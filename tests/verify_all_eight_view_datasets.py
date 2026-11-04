import os
import re
import ctypes as C
import numpy as np
import cv2

DLL_PATH = r"D:\Projects\opencvProject\WaferCalibSDK\build\Release\WaferCalibSDK.dll"
OPENCV_BIN = r"D:\DevSDKs\opencv4100\opencv\build\x64\vc16\bin"
os.add_dll_directory(OPENCV_BIN)

class WaferEightViewEdgeCircleResult(C.Structure):
    _fields_ = [
        ("center_x_mm", C.c_double),
        ("center_y_mm", C.c_double),
        ("radius_mm", C.c_double),
        ("rms_residual_um", C.c_double),
        ("max_residual_um", C.c_double),
        ("notch_angle_deg", C.c_double)
    ]

lib = C.CDLL(DLL_PATH)
eight_view_func = lib.Wafer_FindEightViewEdgeCircle
eight_view_func.argtypes = [
    C.POINTER(C.POINTER(C.c_ubyte)),
    C.POINTER(C.c_double),
    C.c_int,
    C.c_int,
    C.c_double,
    C.POINTER(WaferEightViewEdgeCircleResult)
]
eight_view_func.restype = C.c_int

def run_dataset(dataset_path, pixel_size_um):
    files = [None] * 8
    stage_xy = [0.0] * 16
    for f in os.listdir(dataset_path):
        m = re.match(r'^(\d+)_([-\d\.]+)_([-\d\.]+?)(?:[\s\(].*|\.(?:bmp|jpg))$', f)
        if m and (f.endswith('.bmp') or f.endswith('.jpg')):
            idx = int(m.group(1))
            files[idx] = os.path.join(dataset_path, f)
            stage_xy[idx * 2] = float(m.group(2))
            stage_xy[idx * 2 + 1] = float(m.group(3))
            
    if any(x is None for x in files):
        print(f"Error: dataset {dataset_path} is missing views!")
        return None
        
    imgs = [cv2.imdecode(np.fromfile(f, dtype=np.uint8), cv2.IMREAD_GRAYSCALE) for f in files]
    h, w = imgs[0].shape
    
    img_ptrs = (C.POINTER(C.c_ubyte) * 8)()
    for i in range(8):
        img_ptrs[i] = imgs[i].ctypes.data_as(C.POINTER(C.c_ubyte))
        
    stage_array = (C.c_double * 16)(*stage_xy)
    res = WaferEightViewEdgeCircleResult()
    
    code = eight_view_func(img_ptrs, stage_array, w, h, pixel_size_um, C.byref(res))
    return code, res

if __name__ == '__main__':
    datasets = [
        (r'D:\images\谷神星\标准化\EdgeImage', 1.8),
        (r'D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage_2.5X_8', 1.8),
        (r'D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage_2.5X_thick_8', 1.8),
        (r'D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage_1.5X_thick_8', 3.0),
        (r'D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage', 1.8), # 新目标数据集
    ]
    
    print("=" * 80)
    print("WaferCalibSDK.dll Eight-View Real Dataset Benchmark Verification")
    print("=" * 80)
    
    all_pass = True
    for path, pixel_size in datasets:
        name = os.path.basename(path)
        parent = os.path.basename(os.path.dirname(path))
        label = f"{parent}\\{name}" if parent == "8视野圆弧求圆心" else name
        code, r = run_dataset(path, pixel_size)
        if code == 0:
            print(f"PASS [{label:<35}] (px={pixel_size:.1f}um):")
            print(f"   Center: ({r.center_x_mm:.4f}, {r.center_y_mm:.4f}) mm")
            print(f"   Radius: {r.radius_mm:.4f} mm")
            print(f"   RMS Residual: {r.rms_residual_um:.2f} um, Max Residual: {r.max_residual_um:.2f} um")
            print(f"   Notch Angle: {r.notch_angle_deg:.4f} deg")
        else:
            print(f"FAIL [{label:<35}] Return Code: {code}")
            all_pass = False
        print("-" * 80)
        
    if all_pass:
        print("ALL 5 DATASETS PASSED WITH 100% SUCCESS!")
    else:
        print("SOME DATASETS FAILED!")
