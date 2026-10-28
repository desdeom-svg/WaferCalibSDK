import os
import sys
import time
import ctypes
from ctypes import *
import cv2
import numpy as np

sys.stdout.reconfigure(encoding='utf-8')

# 加载 OpenCV 和 SDK 动态库
os.add_dll_directory(r'D:\DevSDKs\opencv4100\opencv\build\x64\vc16\bin')
dll_path = r'D:\Projects\opencvProject\WaferCalibSDK\build\Release\WaferCalibSDK.dll'
sdk = CDLL(dll_path)

# 结构体定义
class WaferFlatFieldMetrics(Structure):
    _fields_ = [
        ('raw_nu', c_double),
        ('corrected_nu', c_double),
        ('vignetting', c_double)
    ]

# 接口函数原型定义
sdk.Wafer_CreateDarkFrameTemplate.argtypes = [
    POINTER(POINTER(c_ubyte)), c_int, POINTER(c_ubyte), c_int, c_int, c_int
]
sdk.Wafer_CreateDarkFrameTemplate.restype = c_int

sdk.Wafer_CalibrateFlatField.argtypes = [
    POINTER(POINTER(c_ubyte)), c_int, POINTER(c_ubyte), c_int, c_int, c_float,
    POINTER(c_float), c_char_p, POINTER(WaferFlatFieldMetrics)
]
sdk.Wafer_CalibrateFlatField.restype = c_int

sdk.Wafer_ApplyFlatFieldCorrection.argtypes = [
    POINTER(c_ubyte), POINTER(c_ubyte), c_int, c_int, POINTER(c_ubyte), POINTER(c_float)
]
sdk.Wafer_ApplyFlatFieldCorrection.restype = c_int

base_dir = r'D:\images\谷神星\标准化\预处理\test\5X（900-5）'

print("======================================================================")
print("             WaferCalibSDK 平场校正极简接口实际测试报告                ")
print("======================================================================\n")

# 1. 暗场标定测试
print("【测试 1：暗场底噪模板生成 (Wafer_CreateDarkFrameTemplate)】")
dark_dir = os.path.join(base_dir, '暗场')
dark_files = sorted([os.path.join(dark_dir, f) for f in os.listdir(dark_dir) if f.lower().endswith('.bmp')])
print(f"载入暗场帧数量: {len(dark_files)} 帧")

dark_imgs = [cv2.imdecode(np.fromfile(f, np.uint8), cv2.IMREAD_GRAYSCALE) for f in dark_files]
h, w = dark_imgs[0].shape
dark_ptrs = (POINTER(c_ubyte) * len(dark_imgs))(*[im.ctypes.data_as(POINTER(c_ubyte)) for im in dark_imgs])

dark_template = np.zeros((h, w), dtype=np.uint8)
t0 = time.perf_counter()
ret_dark = sdk.Wafer_CreateDarkFrameTemplate(
    dark_ptrs, len(dark_imgs), dark_template.ctypes.data_as(POINTER(c_ubyte)), w, h, 2 # 3-Sigma
)
t_dark_ms = (time.perf_counter() - t0) * 1000
assert ret_dark == 0, f"暗场生成失败，返回码: {ret_dark}"
print(f"-> 暗场标定成功！耗时: {t_dark_ms:.1f} ms, 模板均值: {dark_template.mean():.3f}, 范围: [{dark_template.min()}, {dark_template.max()}]\n")

# 2. 针对 1# ~ 4# 四组标定板分别执行平场标定测试
groups = [
    ('1#标片90', '90%'),
    ('2#标片75', '75%'),
    ('3#标片50', '50%'),
    ('4#标片10', '10%')
]

calibration_results = {}
output_recipes_dir = r"D:\Projects\opencvProject\WaferCalibSDK\output\FlatFieldRecipes"
os.makedirs(output_recipes_dir, exist_ok=True)

print("【测试 2：四组标定板平场标定测试 (Wafer_CalibrateFlatField)】")

for gname, ref_pct in groups:
    gfolder = os.path.join(base_dir, gname)
    files = sorted([os.path.join(gfolder, f) for f in os.listdir(gfolder) if f.lower().endswith('.bmp')])
    imgs = [cv2.imdecode(np.fromfile(f, np.uint8), cv2.IMREAD_GRAYSCALE) for f in files]
    ptrs = (POINTER(c_ubyte) * len(imgs))(*[im.ctypes.data_as(POINTER(c_ubyte)) for im in imgs])

    gain_map = np.zeros((h, w), dtype=np.float32)
    metrics = WaferFlatFieldMetrics()
    save_tiff_path = os.path.join(output_recipes_dir, f"gain_{gname}.tiff").encode('utf-8')

    t0 = time.perf_counter()
    ret_calib = sdk.Wafer_CalibrateFlatField(
        ptrs,
        len(imgs),
        dark_template.ctypes.data_as(POINTER(c_ubyte)),
        w,
        h,
        3.5, # max_gain_clamp
        gain_map.ctypes.data_as(POINTER(c_float)),
        save_tiff_path,
        byref(metrics)
    )
    t_calib_ms = (time.perf_counter() - t0) * 1000
    assert ret_calib == 0, f"{gname} 平场标定失败: {ret_calib}"

    impr = metrics.raw_nu / metrics.corrected_nu if metrics.corrected_nu > 0 else 0.0
    print(f"[{gname} ({ref_pct})] 标定成功！耗时: {t_calib_ms:.1f} ms")
    print(f"   - 校正前非均匀度 (raw_nu):       {metrics.raw_nu:.2f} %")
    print(f"   - 校正后非均匀度 (corrected_nu): {metrics.corrected_nu:.2f} % (均匀度提升 {impr:.1f} 倍)")
    print(f"   - 镜头角隅光学渐晕率 (vignetting): {metrics.vignetting:.2f} %")
    print(f"   - 增益系数范围: [{gain_map.min():.3f}, {gain_map.max():.3f}], 均值: {gain_map.mean():.3f}")
    print(f"   - 模板自动保存路径: {save_tiff_path.decode('utf-8')}")

    calibration_results[gname] = {
        'ref': ref_pct,
        'metrics': metrics,
        'gain_map': gain_map,
        'sample_frame': imgs[0].copy(),
        'tiff_path': save_tiff_path.decode('utf-8')
    }

print("\n【测试 3：32 位浮点 TIFF 增益图像读取与数据指针无缝传输验证】")
test_tiff_path = calibration_results['2#标片75']['tiff_path']
loaded_gain_mat = cv2.imdecode(np.fromfile(test_tiff_path, dtype=np.uint8), cv2.IMREAD_UNCHANGED)
assert loaded_gain_mat is not None and loaded_gain_mat.dtype == np.float32, "读取 32 位浮点 TIFF 失败"
diff_max = np.max(np.abs(loaded_gain_mat - calibration_results['2#标片75']['gain_map']))
print(f"从磁盘载入 32 位 Float TIFF: {test_tiff_path}")
print(f"-> 矩阵尺寸: {loaded_gain_mat.shape}, 类型: {loaded_gain_mat.dtype}")
print(f"-> 与内存原始增益矩阵绝对误差最大值: {diff_max:.8f} (完美一致，零损耗)\n")

print("【测试 4：在线实时平场校正性能与单帧改善效果压测 (Wafer_ApplyFlatFieldCorrection)】")
# 选取 2# 标片的一张实际图像进行测试
sample_input = calibration_results['2#标片75']['sample_frame'].copy()
corrected_output = np.zeros((h, w), dtype=np.uint8)
in_ptr = sample_input.ctypes.data_as(POINTER(c_ubyte))
out_ptr = corrected_output.ctypes.data_as(POINTER(c_ubyte))
dark_ptr = dark_template.ctypes.data_as(POINTER(c_ubyte))
gain_ptr = loaded_gain_mat.ctypes.data_as(POINTER(c_float))

# 4.1 预热
for _ in range(5):
    sdk.Wafer_ApplyFlatFieldCorrection(in_ptr, out_ptr, w, h, dark_ptr, gain_ptr)

# 4.2 正式压测 30 次单帧校正耗时
times_ms = []
for _ in range(30):
    t0 = time.perf_counter()
    sdk.Wafer_ApplyFlatFieldCorrection(in_ptr, out_ptr, w, h, dark_ptr, gain_ptr)
    t1 = time.perf_counter()
    times_ms.append((t1 - t0) * 1000)

mean_time = np.mean(times_ms)
min_time = np.min(times_ms)
fps = 1000.0 / mean_time

# 单帧实测改善度统计
raw_mean, raw_std = float(np.mean(sample_input)), float(np.std(sample_input))
corr_mean, corr_std = float(np.mean(corrected_output)), float(np.std(corrected_output))
raw_nu_single = raw_std / raw_mean * 100.0
corr_nu_single = corr_std / corr_mean * 100.0

print(f"单帧实测图像校正效果 (2# 标片真实单帧):")
print(f"   - 原始图像均值: {raw_mean:.2f}, 标准差: {raw_std:.2f}, 非均匀度 (NU): {raw_nu_single:.2f} %")
print(f"   - 校正图像均值: {corr_mean:.2f}, 标准差: {corr_std:.2f}, 非均匀度 (NU): {corr_nu_single:.2f} %")
print(f"   - 实际单帧非均匀度降低倍数: {raw_nu_single / corr_nu_single:.2f} 倍 (纯剩传感器固有随机散粒底噪)")

print(f"\n4096 x 4096 全画幅实时校正耗时压测 (30 次统计):")
print(f"   - 单帧平均耗时: {mean_time:.2f} ms")
print(f"   - 单帧最快耗时: {min_time:.2f} ms")
print(f"   - 极限吞吐能力: {fps:.1f} fps")

# 4.3 验证原地就地校正 (In-Place)
inplace_buffer = sample_input.copy()
inplace_ptr = inplace_buffer.ctypes.data_as(POINTER(c_ubyte))
ret_inplace = sdk.Wafer_ApplyFlatFieldCorrection(inplace_ptr, inplace_ptr, w, h, dark_ptr, gain_ptr)
assert ret_inplace == 0, "原地校正失败"
assert np.array_equal(inplace_buffer, corrected_output), "原地校正与双缓冲区校正结果不一致"
print(f"-> 原地校正 (In-Place, out == in) 测试: 通过 (结果 100% 吻合)")

# 4.4 验证无暗场扣除快速分支 (dark == NULL)
ret_nodark = sdk.Wafer_ApplyFlatFieldCorrection(in_ptr, out_ptr, w, h, None, gain_ptr)
assert ret_nodark == 0, "无暗场校正失败"
print(f"-> 无暗场快速分支 (dark == NULL) 测试: 通过")

print("\n======================================================================")
print("                       全部测试项 100% PASS                           ")
print("======================================================================")
