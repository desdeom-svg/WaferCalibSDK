# -*- coding: utf-8 -*-
"""
验证 MyImage2.bmp 畸变矫正前后的真实晶圆划片槽直线度与改善率对比
对比三种状态：
  1. 原始未矫正图 (Raw)
  2. 5 视野多视场标定校正图 (Multi-View Corrected)
  3. 单视野标定校正图 (Single-View Corrected)
分析对象：
  - 外侧直线对 (距边缘约 62~64 px，位于镜头畸变最严重的极限边缘)
  - 内侧直线对 (距边缘约 140~142 px)
"""

import os
import ctypes
import cv2
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

plt.rcParams['font.sans-serif'] = ['SimHei', 'Microsoft YaHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

# 1. 加载 DLL
opencv_bin = r'D:\DevSDKs\opencv4100\opencv\build\x64\vc16\bin'
sdk_bin = r'D:\Projects\opencvProject\WaferCalibSDK\build\Release'
if os.path.exists(opencv_bin):
    os.add_dll_directory(opencv_bin)
if os.path.exists(sdk_bin):
    os.add_dll_directory(sdk_bin)

dll_path = os.path.join(sdk_bin, 'WaferCalibSDK.dll')
wafer_dll = ctypes.CDLL(dll_path)

class WaferDotGridDistortionTemplate(ctypes.Structure):
    _fields_ = [
        ("image_width", ctypes.c_int),
        ("image_height", ctypes.c_int),
        ("output_to_input_x", ctypes.c_double * 10),
        ("output_to_input_y", ctypes.c_double * 10),
        ("detected_point_count", ctypes.c_int),
        ("rms_error_pixels", ctypes.c_double)
    ]

wafer_dll.Wafer_CreateDotGridDistortionTemplate.argtypes = [
    ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
    ctypes.c_double, ctypes.POINTER(WaferDotGridDistortionTemplate), ctypes.c_char_p
]
wafer_dll.Wafer_CreateDotGridDistortionTemplate.restype = ctypes.c_int

wafer_dll.Wafer_CreateMultiViewDotGridDistortionTemplate.argtypes = [
    ctypes.POINTER(ctypes.c_char_p), ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
    ctypes.c_double, ctypes.c_double, ctypes.POINTER(WaferDotGridDistortionTemplate), ctypes.c_char_p
]
wafer_dll.Wafer_CreateMultiViewDotGridDistortionTemplate.restype = ctypes.c_int

wafer_dll.Wafer_CorrectImageByDotGridTemplate.argtypes = [
    ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.POINTER(WaferDotGridDistortionTemplate), ctypes.c_char_p
]
wafer_dll.Wafer_CorrectImageByDotGridTemplate.restype = ctypes.c_int

target_dir = r"C:\Users\HYC\Desktop\畸变验证"

def extract_horizontal_ridge_band(img, y_min, y_max, x_range=(200, 3900), step=2):
    """在指定的水平狭带 [y_min, y_max] 内提取高精度亚像素脊线点"""
    pts = []
    for x in range(x_range[0], x_range[1], step):
        sub = img[y_min:y_max+1, x].astype(np.float64)
        if len(sub) < 3:
            continue
        idx = int(np.argmax(sub))
        if idx == 0 or idx == len(sub) - 1:
            continue
        val = sub[idx]
        if val < 160: # 过滤局部遮挡/低对比度
            continue
        # 三点抛物线亚像素插值
        y_m1, y_0, y_p1 = sub[idx - 1], sub[idx], sub[idx + 1]
        denom = 2.0 * (y_m1 - 2.0 * y_0 + y_p1)
        if abs(denom) > 1e-6:
            delta = (y_m1 - y_p1) / denom
            if abs(delta) <= 1.0:
                sub_y = y_min + idx + delta
                pts.append((float(x), float(sub_y)))
                
    pts = np.array(pts, dtype=np.float64)
    if len(pts) < 100:
        return None
        
    # 稳健拟合初步直线，过滤残差大于 1.5 像素的局部划伤/粉尘噪点
    p = np.polyfit(pts[:, 0], pts[:, 1], 1)
    res = np.abs(pts[:, 1] - np.polyval(p, pts[:, 0]))
    inliers = pts[res < 1.5]
    return inliers

def extract_vertical_ridge_band(img, x_min, x_max, y_range=(200, 3900), step=2):
    """在指定的垂直狭带 [x_min, x_max] 内提取高精度亚像素脊线点"""
    pts = []
    for y in range(y_range[0], y_range[1], step):
        sub = img[y, x_min:x_max+1].astype(np.float64)
        if len(sub) < 3:
            continue
        idx = int(np.argmax(sub))
        if idx == 0 or idx == len(sub) - 1:
            continue
        val = sub[idx]
        if val < 160:
            continue
        x_m1, x_0, x_p1 = sub[idx - 1], sub[idx], sub[idx + 1]
        denom = 2.0 * (x_m1 - 2.0 * x_0 + x_p1)
        if abs(denom) > 1e-6:
            delta = (x_m1 - x_p1) / denom
            if abs(delta) <= 1.0:
                sub_x = x_min + idx + delta
                pts.append((float(sub_x), float(y)))
                
    pts = np.array(pts, dtype=np.float64)
    if len(pts) < 100:
        return None
        
    p = np.polyfit(pts[:, 1], pts[:, 0], 1)
    res = np.abs(pts[:, 0] - np.polyval(p, pts[:, 1]))
    inliers = pts[res < 1.5]
    return inliers

def evaluate_line_straightness(pts, is_horizontal=True):
    """
    通过 PCA 正交回归直线拟合，计算：
      1. 直线度 RMS (均方根残差)
      2. 最大偏差 MaxDev (|residual|_max)
      3. 峰谷落差 PV (max - min)
      4. 平均绝对偏差 MAE
      5. 二次弓形弯曲量 Bowing (光学畸变引起的二次弯曲矢高)
    """
    if pts is None or len(pts) < 50:
        return None
        
    centroid = np.mean(pts, axis=0)
    shifted = pts - centroid
    cov = shifted.T @ shifted / len(pts)
    eigvals, eigvecs = np.linalg.eigh(cov)
    
    # normal: 最小特征值对应的法向量
    # direction: 最大特征值对应的主方向
    normal = eigvecs[:, 0]
    direction = eigvecs[:, 1]
    
    # 正交有符号残差
    residuals = shifted @ normal
    
    # 沿直线方向的坐标标尺 t
    t = shifted @ direction
    # 保证 t 方向递增
    if t[-1] < t[0]:
        t = -t
        direction = -direction
        
    # 二次多项式拟合残差提取弯曲矢高
    poly = np.polyfit(t, residuals, 2) # a*t^2 + b*t + c
    t_span = np.max(t) - np.min(t)
    bowing = float(abs(poly[0]) * (t_span / 2.0)**2)
    
    rms = float(np.sqrt(np.mean(residuals**2)))
    max_dev = float(np.max(np.abs(residuals)))
    pv = float(np.max(residuals) - np.min(residuals))
    mae = float(np.mean(np.abs(residuals)))
    
    return {
        'pts_count': len(pts),
        'rms': rms,
        'max_dev': max_dev,
        'pv': pv,
        'mae': mae,
        'bowing': bowing,
        't': t,
        'residuals': residuals,
        'pts': pts,
        'poly': poly
    }

def main():
    print("=================================================================")
    print(" 1. 初始化 5X 畸变标定模型")
    print("=================================================================")
    img0 = cv2.imdecode(np.fromfile(os.path.join(target_dir, "Mark__5X_0.bmp"), dtype=np.uint8), cv2.IMREAD_GRAYSCALE)
    h, w = img0.shape
    
    tpl_single = WaferDotGridDistortionTemplate()
    res_bgr_single = np.zeros((h, w, 3), dtype=np.uint8)
    wafer_dll.Wafer_CreateDotGridDistortionTemplate(
        img0.ctypes.data_as(ctypes.c_char_p), w, h, 10, 10, 0.25, ctypes.byref(tpl_single), res_bgr_single.ctypes.data_as(ctypes.c_char_p)
    )
    
    img_paths = ["Mark__5X_0.bmp", "Mark__5X_2.bmp", "Mark__5X_1.bmp", "Mark__5X_3.bmp", "Mark__5X_4.bmp"]
    imgs_5 = [cv2.imdecode(np.fromfile(os.path.join(target_dir, p), dtype=np.uint8), cv2.IMREAD_GRAYSCALE) for p in img_paths]
    ptrs_5 = (ctypes.c_char_p * 5)(*[im.ctypes.data_as(ctypes.c_char_p) for im in imgs_5])
    tpl_multi = WaferDotGridDistortionTemplate()
    diag_multi = np.zeros((h * 3, w * 3, 3), dtype=np.uint8)
    wafer_dll.Wafer_CreateMultiViewDotGridDistortionTemplate(
        ptrs_5, w, h, 10, 10, 0.25, 0.0, ctypes.byref(tpl_multi), diag_multi.ctypes.data_as(ctypes.c_char_p)
    )
    print(f"标定就绪: 单视野 RMS={tpl_single.rms_error_pixels:.4f} px, 5视野 RMS={tpl_multi.rms_error_pixels:.4f} px")
    
    print("\n=================================================================")
    print(" 2. 执行 MyImage2.bmp 畸变矫正并保存")
    print("=================================================================")
    raw = cv2.imdecode(np.fromfile(os.path.join(target_dir, "MyImage2.bmp"), dtype=np.uint8), cv2.IMREAD_GRAYSCALE)
    
    corr_multi = np.zeros_like(raw)
    wafer_dll.Wafer_CorrectImageByDotGridTemplate(
        raw.ctypes.data_as(ctypes.c_char_p), w, h, ctypes.byref(tpl_multi), corr_multi.ctypes.data_as(ctypes.c_char_p)
    )
    save_multi = os.path.join(target_dir, "MyImage2_corrected_multi.bmp")
    cv2.imencode('.bmp', corr_multi)[1].tofile(save_multi)
    
    corr_single = np.zeros_like(raw)
    wafer_dll.Wafer_CorrectImageByDotGridTemplate(
        raw.ctypes.data_as(ctypes.c_char_p), w, h, ctypes.byref(tpl_single), corr_single.ctypes.data_as(ctypes.c_char_p)
    )
    save_single = os.path.join(target_dir, "MyImage2_corrected_single.bmp")
    cv2.imencode('.bmp', corr_single)[1].tofile(save_single)
    print(f"校正图像已保存: \n  - {save_multi}\n  - {save_single}")

    print("\n=================================================================")
    print(" 3. 提取边缘直线并计算高精度直线度指标")
    print("=================================================================")
    
    # 评测目标线配置
    # (特征名称, 类别, 搜索带min, 搜索带max)
    line_targets = [
        ("外侧顶部水平线 (Y~62)", "top", 58, 68),
        ("外侧左侧垂直线 (X~64)", "left", 60, 70),
        ("内侧顶部水平线 (Y~140)", "top", 134, 146),
        ("内侧左侧垂直线 (X~142)", "left", 136, 148),
    ]
    
    image_sets = [
        ("Raw", "原始未校正 (Raw)", raw),
        ("Single", "单图标定校正 (Single-View)", corr_single),
        ("Multi", "5图多视野校正 (Multi-View)", corr_multi)
    ]
    
    results = {}
    
    for t_name, t_type, b_min, b_max in line_targets:
        results[t_name] = {}
        for code, label, im in image_sets:
            if t_type == "top":
                pts = extract_horizontal_ridge_band(im, b_min, b_max)
                stat = evaluate_line_straightness(pts, is_horizontal=True)
            else:
                pts = extract_vertical_ridge_band(im, b_min, b_max)
                stat = evaluate_line_straightness(pts, is_horizontal=False)
            results[t_name][code] = stat

    # 4. 打印量化对比表
    print("\n" + "=" * 115)
    print(f"{'评测直线特征':<24} | {'指标项':<12} | {'未校正 (Raw)':<12} | {'单图校正 (Single)':<14} | {'5图校正 (Multi)':<14} | {'Multi改善率 (%)':<10}")
    print("=" * 115)
    
    summary_data = []
    
    for t_name, _ in [
        ("外侧顶部水平线 (Y~62)", "top"),
        ("外侧左侧垂直线 (X~64)", "left"),
        ("内侧顶部水平线 (Y~140)", "top"),
        ("内侧左侧垂直线 (X~142)", "left"),
    ]:
        raw_st = results[t_name]["Raw"]
        single_st = results[t_name]["Single"]
        multi_st = results[t_name]["Multi"]
        
        # 改善率函数
        def calc_impr(v_raw, v_corr):
            return (v_raw - v_corr) / v_raw * 100.0
            
        metrics = [
            ("直线度 RMS", raw_st['rms'], single_st['rms'], multi_st['rms']),
            ("最大偏差 MaxDev", raw_st['max_dev'], single_st['max_dev'], multi_st['max_dev']),
            ("峰谷跨度 PV", raw_st['pv'], single_st['pv'], multi_st['pv']),
            ("二次弓高 Bowing", raw_st['bowing'], single_st['bowing'], multi_st['bowing'])
        ]
        
        for idx, (m_name, v_r, v_s, v_m) in enumerate(metrics):
            impr_m = calc_impr(v_r, v_m)
            prefix = t_name if idx == 0 else ""
            print(f"{prefix:<24} | {m_name:<12} | {v_r:10.4f} px | {v_s:12.4f} px | {v_m:12.4f} px | {impr_m:8.1f}%")
            summary_data.append((t_name, m_name, v_r, v_s, v_m, impr_m))
        print("-" * 115)

    print("\n=================================================================")
    print(" 5. 正在生成高分辨率残差对比曲线图")
    print("=================================================================")
    
    fig, axes = plt.subplots(2, 2, figsize=(18, 12), dpi=150)
    fig.suptitle("晶圆划片槽边缘直线度畸变校正前后对比 (MyImage2.bmp - 5X 物镜)", fontsize=16, fontweight='bold', y=0.98)
    
    plot_items = [
        ("外侧顶部水平线 (Y~62)", axes[0, 0]),
        ("外侧左侧垂直线 (X~64)", axes[0, 1]),
        ("内侧顶部水平线 (Y~140)", axes[1, 0]),
        ("内侧左侧垂直线 (X~142)", axes[1, 1]),
    ]
    
    for t_name, ax in plot_items:
        raw_st = results[t_name]["Raw"]
        single_st = results[t_name]["Single"]
        multi_st = results[t_name]["Multi"]
        
        # 降采样平滑以便观察趋势
        ax.plot(raw_st['t'], raw_st['residuals'], 'r-', alpha=0.3, label=f"原始 (Raw): RMS={raw_st['rms']:.3f}px, 弓高={raw_st['bowing']:.3f}px")
        t_grid = np.linspace(np.min(raw_st['t']), np.max(raw_st['t']), 200)
        ax.plot(t_grid, np.polyval(raw_st['poly'], t_grid), 'r--', linewidth=2.0)
        
        ax.plot(single_st['t'], single_st['residuals'], 'b-', alpha=0.3, label=f"单图校正 (Single): RMS={single_st['rms']:.3f}px, 弓高={single_st['bowing']:.3f}px")
        ax.plot(t_grid, np.polyval(single_st['poly'], t_grid), 'b--', linewidth=2.0)

        ax.plot(multi_st['t'], multi_st['residuals'], 'g-', alpha=0.3, label=f"5图校正 (Multi): RMS={multi_st['rms']:.3f}px, 弓高={multi_st['bowing']:.3f}px")
        ax.plot(t_grid, np.polyval(multi_st['poly'], t_grid), 'g--', linewidth=2.0)
        
        ax.axhline(0, color='black', linestyle=':', linewidth=0.8)
        impr_rms = (raw_st['rms'] - multi_st['rms']) / raw_st['rms'] * 100.0
        impr_bow = (raw_st['bowing'] - multi_st['bowing']) / raw_st['bowing'] * 100.0
        ax.set_title(f"{t_name}\n[RMS改善: {impr_rms:.1f}%, 弓高改善: {impr_bow:.1f}%]", fontsize=12, fontweight='bold')
        ax.set_xlabel("沿边缘直线坐标 (像素)", fontsize=10)
        ax.set_ylabel("正交直线度残差 (像素)", fontsize=10)
        ax.set_ylim(-1.5, 1.5)
        ax.grid(True, linestyle=':', alpha=0.6)
        ax.legend(loc='upper right', fontsize=9)
        
    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    doc_dir = r"d:\Projects\opencvProject\WaferCalibSDK\.ai_docs"
    comp_plot_path = os.path.join(doc_dir, "MyImage2_straightness_comparison.png")
    plt.savefig(comp_plot_path, bbox_inches='tight')
    plt.close()
    print(f"图表已成功保存至: {comp_plot_path}")

if __name__ == "__main__":
    main()
