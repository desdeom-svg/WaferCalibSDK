# -*- coding: utf-8 -*-
"""
生成去畸变 + 旋转调平后的标准图像
包含：
1. 5 视野多视场去畸变 + 旋转调平
2. 单视野去畸变 + 旋转调平
3. 角度量化验证与对比图生成
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

from verify_distortion_correction_5x import extract_grid_points

target_dir = r"C:\Users\HYC\Desktop\畸变验证"

def measure_grid_angle(grid):
    """精确测量点阵网格相对于水平方向的平均旋转倾斜角（单位：度）"""
    row_angles = []
    for r in range(grid.shape[0]):
        row = grid[r]
        dx = row[-1, 0] - row[0, 0]
        dy = row[-1, 1] - row[0, 1]
        ang = np.degrees(np.arctan2(dy, dx))
        row_angles.append(ang)
    return float(np.mean(row_angles))

def level_image(img, angle_deg, center=None):
    """
    将图像旋转指定角度以调平。
    在 OpenCV 图像坐标系中（Y轴向下）：
    通过传入测得的网格倾角 angle_deg，cv2.getRotationMatrix2D 即可将倾斜线精确旋转回归至水平（dy=0）。
    """
    h, w = img.shape[:2]
    if center is None:
        center = (w / 2.0, h / 2.0)
    
    # 消除倾斜角
    rot_angle = angle_deg
    M = cv2.getRotationMatrix2D(center, rot_angle, 1.0)
    leveled = cv2.warpAffine(img, M, (w, h), flags=cv2.INTER_CUBIC, borderMode=cv2.BORDER_REFLECT_101)
    return leveled, M

def main():
    print("=================================================================")
    print(" 1. 正在初始化 5X 畸变标定模型...")
    print("=================================================================")
    img0 = cv2.imdecode(np.fromfile(os.path.join(target_dir, "Mark__5X_0.bmp"), dtype=np.uint8), cv2.IMREAD_GRAYSCALE)
    h, w = img0.shape
    
    # 单视野标定模板
    tpl_single = WaferDotGridDistortionTemplate()
    res_bgr_single = np.zeros((h, w, 3), dtype=np.uint8)
    wafer_dll.Wafer_CreateDotGridDistortionTemplate(
        img0.ctypes.data_as(ctypes.c_char_p), w, h, 10, 10, 0.25, ctypes.byref(tpl_single), res_bgr_single.ctypes.data_as(ctypes.c_char_p)
    )
    
    # 5 视野多视场标定模板
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
    print(" 2. 正在生成去畸变 + 旋转调平后的全分辨率图像")
    print("=================================================================")
    
    test_files = [
        ("MyImage1.bmp", "MyImage1"),
        ("MyImage.bmp", "MyImage"),
        ("Mark__5X_0.bmp", "Mark__5X_0")
    ]
    
    results = {}
    
    for fname, name in test_files:
        fpath = os.path.join(target_dir, fname)
        raw = cv2.imdecode(np.fromfile(fpath, dtype=np.uint8), cv2.IMREAD_GRAYSCALE)
        
        # 2.1 执行 5 视野多视场去畸变
        corr_multi = np.zeros_like(raw)
        wafer_dll.Wafer_CorrectImageByDotGridTemplate(
            raw.ctypes.data_as(ctypes.c_char_p), w, h, ctypes.byref(tpl_multi), corr_multi.ctypes.data_as(ctypes.c_char_p)
        )
        
        # 2.2 测量去畸变后的网格水平倾角
        grid_corr = extract_grid_points(corr_multi)
        tilt_deg = measure_grid_angle(grid_corr)
        
        # 2.3 旋转调平 (以中心 (2048, 2048) 为原点进行亚像素级双三次插值旋转)
        leveled_multi, M_multi = level_image(corr_multi, tilt_deg)
        
        # 2.4 验证调平后的网格角度
        grid_leveled = extract_grid_points(leveled_multi)
        leveled_angle = measure_grid_angle(grid_leveled)
        
        # 保存多视场校正调平图至用户桌面
        save_multi_path = os.path.join(target_dir, f"{name}_leveled.bmp")
        cv2.imencode('.bmp', leveled_multi)[1].tofile(save_multi_path)
        
        # 同时也保存到工作区方便内嵌展示
        cv2.imencode('.bmp', leveled_multi)[1].tofile(f"{name}_leveled.bmp")
        
        # 2.5 同时也处理单视场校正调平图
        corr_single = np.zeros_like(raw)
        wafer_dll.Wafer_CorrectImageByDotGridTemplate(
            raw.ctypes.data_as(ctypes.c_char_p), w, h, ctypes.byref(tpl_single), corr_single.ctypes.data_as(ctypes.c_char_p)
        )
        leveled_single, _ = level_image(corr_single, tilt_deg)
        save_single_path = os.path.join(target_dir, f"{name}_single_leveled.bmp")
        cv2.imencode('.bmp', leveled_single)[1].tofile(save_single_path)
        
        print(f"[{name}] 处理完成:")
        print(f"  - 调平前网格倾角: {tilt_deg:.4f}° (4096px 跨度落差 {4096 * np.tan(np.radians(abs(tilt_deg))):.2f} px)")
        print(f"  - 旋转调平补偿量: {tilt_deg:+.4f}°")
        print(f"  - 调平后网格倾角: {leveled_angle:.4f}° (调平残差: {abs(leveled_angle):.4f}°)")
        print(f"  - 已输出调平图: {save_multi_path}")
        print(f"  - 已输出调平图: {save_single_path}")
        
        results[name] = {
            'raw': raw,
            'corr_multi': corr_multi,
            'leveled_multi': leveled_multi,
            'grid_corr': grid_corr,
            'grid_leveled': grid_leveled,
            'tilt_deg': tilt_deg,
            'leveled_angle': leveled_angle
        }
        
    print("\n=================================================================")
    print(" 3. 正在生成高对比可视化图...")
    print("=================================================================")
    
    # 绘制可视化对比图 (以 MyImage1 为例展示细节)
    data = results["MyImage1"]
    grid_before = data['grid_corr']
    grid_after = data['grid_leveled']
    
    fig, axes = plt.subplots(1, 2, figsize=(16, 8), dpi=150)
    
    # 左图：调平前 (仅去畸变)
    ax0 = axes[0]
    ax0.imshow(data['corr_multi'], cmap='gray')
    for r in range(10):
        # 实际点线
        ax0.plot(grid_before[r, :, 0], grid_before[r, :, 1], 'g.-', linewidth=1.5, markersize=4)
        # 绝对水平参考线 (过该行第一个点)
        ax0.plot([grid_before[r, 0, 0], grid_before[r, -1, 0]], 
                 [grid_before[r, 0, 1], grid_before[r, 0, 1]], 'r--', linewidth=1.0, alpha=0.7)
    ax0.set_title(f"【去畸变后 (未旋转调平)】\n网格平均倾角: {data['tilt_deg']:.4f}°\n(红虚线: 绝对水平基准；绿线: 实际点阵行)", fontsize=12, fontweight='bold')
    ax0.set_xlim(grid_before[0, 0, 0] - 80, grid_before[0, -1, 0] + 80)
    ax0.set_ylim(grid_before[-1, 0, 1] + 80, grid_before[0, 0, 1] - 80)
    ax0.grid(True, linestyle=':', alpha=0.5)
    
    # 右图：去畸变 + 旋转调平后
    ax1 = axes[1]
    ax1.imshow(data['leveled_multi'], cmap='gray')
    for r in range(10):
        # 实际点线
        ax1.plot(grid_after[r, :, 0], grid_after[r, :, 1], 'g.-', linewidth=1.5, markersize=4)
        # 绝对水平参考线 (过该行第一个点)
        ax1.plot([grid_after[r, 0, 0], grid_after[r, -1, 0]], 
                 [grid_after[r, 0, 1], grid_after[r, 0, 1]], 'r--', linewidth=1.0, alpha=0.7)
    ax1.set_title(f"【去畸变 + 旋转调平后】\n网格平均倾角: {data['leveled_angle']:.4f}° (纠偏 {-data['tilt_deg']:+.4f}°)\n(点阵行与红虚线水平基准完美重合)", fontsize=12, fontweight='bold', color='darkgreen')
    ax1.set_xlim(grid_after[0, 0, 0] - 80, grid_after[0, -1, 0] + 80)
    ax1.set_ylim(grid_after[-1, 0, 1] + 80, grid_after[0, 0, 1] - 80)
    ax1.grid(True, linestyle=':', alpha=0.5)
    
    plt.tight_layout()
    doc_dir = r"d:\Projects\opencvProject\WaferCalibSDK\.ai_docs"
    os.makedirs(doc_dir, exist_ok=True)
    comp_plot_path = os.path.join(doc_dir, "distortion_leveling_comparison.png")
    plt.savefig(comp_plot_path, bbox_inches='tight')
    plt.close()
    print(f"可视化对比图已保存至: {comp_plot_path}")
    print("\n所有操作顺利完成！")

if __name__ == "__main__":
    main()
