import ctypes
import os
import time
import cv2
import numpy as np

# 添加 DLL 依赖路径
opencv_bin = r'D:\DevSDKs\opencv4100\opencv\build\x64\vc16\bin'
sdk_bin = r'D:\Projects\opencvProject\WaferCalibSDK\build\Release'
if os.path.exists(opencv_bin):
    os.add_dll_directory(opencv_bin)
if os.path.exists(sdk_bin):
    os.add_dll_directory(sdk_bin)

# 加载 WaferCalibSDK.dll
dll_path = os.path.join(sdk_bin, 'WaferCalibSDK.dll')
wafer_dll = ctypes.CDLL(dll_path)

# 定义 C API 签名
# int Wafer_FindHorizontalLineAngle(const unsigned char* mono8_data, int width, int height, double* out_angle_degrees, unsigned char* out_result_bgr);
wafer_dll.Wafer_FindHorizontalLineAngle.argtypes = [
    ctypes.c_char_p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.POINTER(ctypes.c_double),
    ctypes.c_char_p
]
wafer_dll.Wafer_FindHorizontalLineAngle.restype = ctypes.c_int

# int Wafer_FindTwoViewHorizontalLineAngle(...)
wafer_dll.Wafer_FindTwoViewHorizontalLineAngle.argtypes = [
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_double,
    ctypes.c_double,
    ctypes.POINTER(ctypes.c_double),
    ctypes.POINTER(ctypes.c_double),
    ctypes.POINTER(ctypes.c_double),
    ctypes.c_char_p,
    ctypes.c_int,
    ctypes.c_int
]
wafer_dll.Wafer_FindTwoViewHorizontalLineAngle.restype = ctypes.c_int

print("=================================================================")
print(" [WaferCalibSDK] CameraAjustCalibration 样本集 C API 全功能评测")
print("=================================================================")

target_dir = r"C:\Users\HYC\Desktop\CameraAjustCalibration"
samples = ["Mark_1.bmp", "Mark_2.bmp", "Mark_3.bmp"]

loaded_images = {}

for s in samples:
    path = os.path.join(target_dir, s)
    if not os.path.exists(path):
        print(f"Error: {path} 不存在！")
        continue
    data = np.fromfile(path, dtype=np.uint8)
    img = cv2.imdecode(data, cv2.IMREAD_GRAYSCALE)
    if img is None:
        print(f"Error: 无法解码 {path}")
        continue
    loaded_images[s] = img
    h, w = img.shape
    
    # 分配输出内存
    out_angle = ctypes.c_double(0.0)
    result_bgr = np.zeros((h, w, 3), dtype=np.uint8)
    
    # 计时评测
    t0 = time.perf_counter()
    code = wafer_dll.Wafer_FindHorizontalLineAngle(
        img.ctypes.data_as(ctypes.c_char_p),
        w,
        h,
        ctypes.byref(out_angle),
        result_bgr.ctypes.data_as(ctypes.c_char_p)
    )
    t1 = time.perf_counter()
    cost_ms = (t1 - t0) * 1000.0
    
    status_str = "SUCCESS" if code == 0 else f"FAILED(code={code})"
    print(f"\n[{s}] 4096 x 4096:")
    print(f"  - 识别状态: {status_str}")
    print(f"  - 测得角度: {out_angle.value:.4f} 度 ({out_angle.value * 60.0:.2f} 角分)")
    print(f"  - 运算耗时: {cost_ms:.2f} ms")
    
    # 保存缩略结果图
    out_preview_path = f"output_{s[:-4]}_result_preview.jpg"
    small_bgr = cv2.resize(result_bgr, (1024, 1024))
    cv2.imwrite(out_preview_path, small_bgr)
    print(f"  - 诊断图已生成: {out_preview_path}")

# 双视野大基线测试：Mark_2 (左视场) -> Mark_1 (右视场)
if "Mark_1.bmp" in loaded_images and "Mark_2.bmp" in loaded_images:
    print("\n-----------------------------------------------------------------")
    print(" [双视野大基线评测] View1: Mark_2 (左视野) | View2: Mark_1 (右视野)")
    print("-----------------------------------------------------------------")
    img_v1 = loaded_images["Mark_2.bmp"]
    img_v2 = loaded_images["Mark_1.bmp"]
    h, w = img_v1.shape
    
    stage_delta_x_mm = 200.0  # 假设机台平移 200 mm
    pixel_scale_y_um = 1.0    # 假设 1.0 um/px
    
    out_global = ctypes.c_double(0.0)
    out_v1 = ctypes.c_double(0.0)
    out_v2 = ctypes.c_double(0.0)
    diag_w = 3000
    diag_h = 1800
    diag_bgr = np.zeros((diag_h, diag_w, 3), dtype=np.uint8)
    
    t0 = time.perf_counter()
    code = wafer_dll.Wafer_FindTwoViewHorizontalLineAngle(
        img_v1.ctypes.data_as(ctypes.c_char_p),
        img_v2.ctypes.data_as(ctypes.c_char_p),
        w,
        h,
        stage_delta_x_mm,
        pixel_scale_y_um,
        ctypes.byref(out_global),
        ctypes.byref(out_v1),
        ctypes.byref(out_v2),
        diag_bgr.ctypes.data_as(ctypes.c_char_p),
        diag_w,
        diag_h
    )
    t1 = time.perf_counter()
    cost_ms = (t1 - t0) * 1000.0
    
    print(f"  - 双视野解算状态: {'SUCCESS' if code == 0 else f'FAILED(code={code})'}")
    print(f"  - 全局大基线角度: {out_global.value:.4f} 度 ({out_global.value * 60.0:.2f} 角分)")
    print(f"  - View1 局部角度: {out_v1.value:.4f} 度")
    print(f"  - View2 局部角度: {out_v2.value:.4f} 度")
    print(f"  - 双视野解算耗时: {cost_ms:.2f} ms")
    
    two_view_preview = "output_two_view_Mark2_Mark1_diag.jpg"
    cv2.imwrite(two_view_preview, diag_bgr)
    print(f"  - 3000x1800 综合看板已生成: {two_view_preview}")

print("\n=================================================================")
print(" 全部评测圆满完成！")
print("=================================================================")
