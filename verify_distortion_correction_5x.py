import ctypes
import os
import cv2
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

# 设置字体以防中文方块
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

def extract_grid_points(img, expected_cols=10, expected_rows=10):
    mean_val = np.mean(img)
    std_val = np.std(img)
    thresh_val = mean_val + std_val * 1.5
    _, fg = cv2.threshold(img, thresh_val, 255, cv2.THRESH_BINARY)
    
    n_labels, labels, stats, centroids = cv2.connectedComponentsWithStats(fg, 8)
    blobs = []
    for l in range(1, n_labels):
        area = stats[l, cv2.CC_STAT_AREA]
        bw = stats[l, cv2.CC_STAT_WIDTH]
        bh = stats[l, cv2.CC_STAT_HEIGHT]
        if 500 < area < 5000 and 0.6 < bw / max(1, bh) < 1.6:
            sub = img[stats[l, cv2.CC_STAT_TOP]:stats[l, cv2.CC_STAT_TOP]+bh,
                      stats[l, cv2.CC_STAT_LEFT]:stats[l, cv2.CC_STAT_LEFT]+bw].astype(np.float64)
            mask = (labels[stats[l, cv2.CC_STAT_TOP]:stats[l, cv2.CC_STAT_TOP]+bh,
                           stats[l, cv2.CC_STAT_LEFT]:stats[l, cv2.CC_STAT_LEFT]+bw] == l)
            sub_fg = np.where(mask, sub - np.min(sub), 0.0)
            total_mass = np.sum(sub_fg)
            if total_mass > 0:
                yy, xx = np.indices(sub_fg.shape)
                cx = stats[l, cv2.CC_STAT_LEFT] + np.sum(xx * sub_fg) / total_mass
                cy = stats[l, cv2.CC_STAT_TOP] + np.sum(yy * sub_fg) / total_mass
            else:
                cx, cy = centroids[l][0], centroids[l][1]
            blobs.append((cx, cy, area))
            
    pts = np.array([(b[0], b[1]) for b in blobs], dtype=np.float64)
    if len(pts) < 100:
        raise ValueError(f"Found only {len(pts)} blobs")
        
    dists = []
    for idx in range(len(pts)):
        d = np.linalg.norm(pts - pts[idx], axis=1)
        d_nonzero = d[d > 10.0]
        if len(d_nonzero) > 0:
            dists.append(np.min(d_nonzero))
    pitch = np.median(dists)
    
    found_cm = None
    for idx in range(len(pts)):
        d = np.linalg.norm(pts - pts[idx], axis=1)
        diag_neighbors = np.where((d >= pitch * 0.65) & (d <= pitch * 0.85))[0]
        if len(diag_neighbors) == 4:
            found_cm = pts[idx]
            break
            
    if found_cm is None:
        raise ValueError("Center mark not found!")
        
    grid = np.full((expected_rows, expected_cols, 2), np.nan, dtype=np.float64)
    for pt in pts:
        if np.linalg.norm(pt - found_cm) < pitch * 0.2:
            continue
        dc = (pt[0] - found_cm[0]) / pitch
        dr = (pt[1] - found_cm[1]) / pitch
        c_idx = int(np.round(dc + 4.5))
        r_idx = int(np.round(dr + 4.5))
        if 0 <= c_idx < expected_cols and 0 <= r_idx < expected_rows:
            grid[r_idx, c_idx] = pt
            
    return grid

def analyze_grid_metrics(grid):
    rows, cols, _ = grid.shape
    
    # 1. 每行直线度
    row_residuals = []
    for r in range(rows):
        pts = grid[r, :, :]
        x, y = pts[:, 0], pts[:, 1]
        p = np.polyfit(x, y, 1)
        dist = np.abs(p[0] * x - y + p[1]) / np.sqrt(p[0]**2 + 1.0)
        row_residuals.extend(dist)
    row_residuals = np.array(row_residuals)
    
    # 2. 每列直线度
    col_residuals = []
    for c in range(cols):
        pts = grid[:, c, :]
        x, y = pts[:, 0], pts[:, 1]
        p = np.polyfit(y, x, 1)
        dist = np.abs(p[0] * y - x + p[1]) / np.sqrt(p[0]**2 + 1.0)
        col_residuals.extend(dist)
    col_residuals = np.array(col_residuals)
    
    all_straightness = np.concatenate([row_residuals, col_residuals])
    
    # 3. 2D 仿射网格模型 (严格剔除线性缩放、剪切与旋转，求取纯非线性光学畸变残差)
    C, R = np.meshgrid(np.arange(cols) - 4.5, np.arange(rows) - 4.5)
    c_flat = C.flatten()
    r_flat = R.flatten()
    X_flat = grid[:, :, 0].flatten()
    Y_flat = grid[:, :, 1].flatten()
    
    M = np.column_stack([c_flat, r_flat, np.ones_like(c_flat)])
    coeff_x, _, _, _ = np.linalg.lstsq(M, X_flat, rcond=None)
    coeff_y, _, _, _ = np.linalg.lstsq(M, Y_flat, rcond=None)
    
    ideal_x = M @ coeff_x
    ideal_y = M @ coeff_y
    
    diff_x = X_flat - ideal_x
    diff_y = Y_flat - ideal_y
    dist_error = np.sqrt(diff_x**2 + diff_y**2)
    
    return {
        'row_rms': float(np.sqrt(np.mean(row_residuals**2))),
        'row_max': float(np.max(row_residuals)),
        'col_rms': float(np.sqrt(np.mean(col_residuals**2))),
        'col_max': float(np.max(col_residuals)),
        'straightness_rms': float(np.sqrt(np.mean(all_straightness**2))),
        'straightness_max': float(np.max(all_straightness)),
        'affine_rms': float(np.sqrt(np.mean(dist_error**2))),
        'affine_max': float(np.max(dist_error)),
        'affine_mean': float(np.mean(dist_error)),
        'diff_x': diff_x,
        'diff_y': diff_y,
        'dist_error': dist_error,
        'ideal_pts': np.column_stack([ideal_x, ideal_y]),
        'meas_pts': np.column_stack([X_flat, Y_flat])
    }

print("=================================================================")
print(" [WaferCalibSDK] 5X 畸变标定与 MyImage / MyImage1 验证全流程启动")
print("=================================================================")

# 1. 标定
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

print(f"标定完成:\n  - 单视野标定模板: 拟合点数={tpl_single.detected_point_count}, RMS={tpl_single.rms_error_pixels:.4f} px")
print(f"  - 5视野标定模板:  拟合点数={tpl_multi.detected_point_count}, RMS={tpl_multi.rms_error_pixels:.4f} px")

# 2. 评估
all_data = {}
for name in ["MyImage.bmp", "MyImage1.bmp"]:
    raw = cv2.imdecode(np.fromfile(os.path.join(target_dir, name), dtype=np.uint8), cv2.IMREAD_GRAYSCALE)
    
    corr_s = np.zeros_like(raw)
    wafer_dll.Wafer_CorrectImageByDotGridTemplate(
        raw.ctypes.data_as(ctypes.c_char_p), w, h, ctypes.byref(tpl_single), corr_s.ctypes.data_as(ctypes.c_char_p)
    )
    
    corr_m = np.zeros_like(raw)
    wafer_dll.Wafer_CorrectImageByDotGridTemplate(
        raw.ctypes.data_as(ctypes.c_char_p), w, h, ctypes.byref(tpl_multi), corr_m.ctypes.data_as(ctypes.c_char_p)
    )
    
    m_raw = analyze_grid_metrics(extract_grid_points(raw))
    m_s = analyze_grid_metrics(extract_grid_points(corr_s))
    m_m = analyze_grid_metrics(extract_grid_points(corr_m))
    
    all_data[name] = {
        'raw': m_raw,
        'single': m_s,
        'multi': m_m
    }

# 3. 打印综合报告
print("\n" + "="*80)
print(" 畸变矫正前后定量对比结果汇总表 (单位: 像素 px)")
print("="*80)

for name in ["MyImage.bmp", "MyImage1.bmp"]:
    d = all_data[name]
    print(f"\n>>> 样本: {name} <<<")
    headers = ["评价指标", "矫正前 (Raw)", "单图标定校正", "改善率 (单图)", "5图多视野校正", "改善率 (5图)"]
    print(f"{headers[0]:<22} | {headers[1]:<12} | {headers[2]:<12} | {headers[3]:<12} | {headers[4]:<12} | {headers[5]:<12}")
    print("-" * 96)
    
    metrics_list = [
        ("综合直线度 RMS", 'straightness_rms'),
        ("最大直线度偏差", 'straightness_max'),
        ("行向直线度 RMS", 'row_rms'),
        ("最大行直线偏差", 'row_max'),
        ("列向直线度 RMS", 'col_rms'),
        ("最大列直线偏差", 'col_max'),
        ("纯非线性畸变 RMS", 'affine_rms'),
        ("纯非线性畸变最大值", 'affine_max'),
        ("纯非线性畸变平均值", 'affine_mean'),
    ]
    
    for label, k in metrics_list:
        v0 = d['raw'][k]
        vs = d['single'][k]
        vm = d['multi'][k]
        is_val = (v0 - vs) / v0 * 100.0
        im_val = (v0 - vm) / v0 * 100.0
        print(f"{label:<20} | {v0:<12.4f} | {vs:<12.4f} | {is_val:>10.1f}% | {vm:<12.4f} | {im_val:>10.1f}%")

# 4. 生成可视化诊断对比大图
print("\n[3] 正在生成可视化对比图表...")
for name in ["MyImage.bmp", "MyImage1.bmp"]:
    d = all_data[name]
    fig, axes = plt.subplots(1, 3, figsize=(20, 6.5), dpi=150)
    
    # 状态列表
    states = [('raw', '原始未矫正 (Raw)', 'red'),
              ('single', '单图标定矫正后', 'blue'),
              ('multi', '5图多视野标定矫正后', 'green')]
    
    for idx, (st_key, title, color) in enumerate(states):
        ax = axes[idx]
        cur_m = d[st_key]
        pts = cur_m['meas_pts']
        diff_x = cur_m['diff_x']
        diff_y = cur_m['diff_y']
        err = cur_m['dist_error']
        
        # 散点图显示网格
        sc = ax.scatter(pts[:, 0], pts[:, 1], c=err, cmap='jet', s=35, edgecolors='k', linewidth=0.5, vmin=0, vmax=0.6)
        
        # 矢量图: 放大 300 倍显示畸变位移方向与大小
        ax.quiver(pts[:, 0], pts[:, 1], diff_x, diff_y, color='black', scale=0.003, scale_units='xy', width=0.003)
        
        ax.set_title(f"{title}\n直线度RMS: {cur_m['straightness_rms']:.4f} px | 非线性RMS: {cur_m['affine_rms']:.4f} px\n最大偏差: {cur_m['affine_max']:.4f} px", fontsize=11)
        ax.set_xlim([0, 4096])
        ax.set_ylim([4096, 0]) # 图像坐标系 Y 轴向下
        ax.set_aspect('equal')
        ax.grid(True, linestyle='--', alpha=0.5)
        if idx == 2:
            cbar = plt.colorbar(sc, ax=ax, fraction=0.046, pad=0.04)
            cbar.set_label('非线性残差 (像素)', fontsize=10)

    fig.suptitle(f"[{name}] 畸变矫正前后几何精度与位移矢量场对比分析 (Quiver 放大 300x)", fontsize=14, y=0.98)
    plt.tight_layout()
    out_chart = f"distortion_comparison_{name[:-4]}.png"
    plt.savefig(out_chart)
    plt.close()
    print(f"  图表已保存: {out_chart}")

print("\n全部分析与图表生成完毕！")
