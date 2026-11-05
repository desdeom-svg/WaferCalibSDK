import os
import re
import time
import ctypes as C
import numpy as np
import cv2

DLL_PATH = r"D:\Projects\opencvProject\WaferCalibSDK\build\Release\WaferCalibSDK.dll"
OPENCV_BIN = r"D:\DevSDKs\opencv4100\opencv\build\x64\vc16\bin"
os.add_dll_directory(OPENCV_BIN)


class WaferEdgeCircleResult(C.Structure):
    _fields_ = [
        ("center_x_mm", C.c_double),
        ("center_y_mm", C.c_double),
        ("radius_mm", C.c_double),
        ("rms_residual_um", C.c_double),
        ("max_residual_um", C.c_double),
    ]


class WaferEightViewEdgeCircleResult(C.Structure):
    _fields_ = [
        ("center_x_mm", C.c_double),
        ("center_y_mm", C.c_double),
        ("radius_mm", C.c_double),
        ("rms_residual_um", C.c_double),
        ("max_residual_um", C.c_double),
        ("notch_angle_deg", C.c_double),
    ]


lib = C.CDLL(DLL_PATH)

four_view_func = lib.Wafer_FindFourViewEdgeCircle
four_view_func.argtypes = [
    C.POINTER(C.POINTER(C.c_ubyte)),
    C.POINTER(C.c_double),
    C.c_int,
    C.c_int,
    C.c_double,
    C.POINTER(WaferEdgeCircleResult),
]
four_view_func.restype = C.c_int

eight_view_func = lib.Wafer_FindEightViewEdgeCircle
eight_view_func.argtypes = [
    C.POINTER(C.POINTER(C.c_ubyte)),
    C.POINTER(C.c_double),
    C.c_int,
    C.c_int,
    C.c_double,
    C.POINTER(WaferEightViewEdgeCircleResult),
]
eight_view_func.restype = C.c_int


def run_eight_view_dataset(dataset_path, pixel_size_um):
    files = [None] * 8
    stage_xy = [0.0] * 16
    for f in os.listdir(dataset_path):
        m = re.match(r'^(\d+)_([-\d\.]+)_([-\d\.]+?)(?:[\s\(].*|\.(?:bmp|jpg))$', f)
        if m and f.lower().endswith(('.bmp', '.jpg')):
            idx = int(m.group(1))
            if idx < 8:
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

    t0 = time.perf_counter()
    code = eight_view_func(img_ptrs, stage_array, w, h, pixel_size_um, C.byref(res))
    dt_ms = (time.perf_counter() - t0) * 1000.0
    return code, res, dt_ms


def run_four_view_dataset(dataset_path, pixel_size_um):
    files = [None] * 4
    stage_xy = [0.0] * 8
    for f in os.listdir(dataset_path):
        m = re.match(r'^(\d+)_([-\d\.]+)_([-\d\.]+?)(?:[\s\(].*|\.(?:bmp|jpg))$', f)
        if m and f.lower().endswith(('.bmp', '.jpg')):
            idx = int(m.group(1))
            if idx < 4:
                files[idx] = os.path.join(dataset_path, f)
                stage_xy[idx * 2] = float(m.group(2))
                stage_xy[idx * 2 + 1] = float(m.group(3))

    if any(x is None for x in files):
        print(f"Error: dataset {dataset_path} is missing 4-view images!")
        return None

    imgs = [cv2.imdecode(np.fromfile(f, dtype=np.uint8), cv2.IMREAD_GRAYSCALE) for f in files]
    h, w = imgs[0].shape

    img_ptrs = (C.POINTER(C.c_ubyte) * 4)()
    for i in range(4):
        img_ptrs[i] = imgs[i].ctypes.data_as(C.POINTER(C.c_ubyte))

    stage_array = (C.c_double * 8)(*stage_xy)
    res = WaferEdgeCircleResult()

    t0 = time.perf_counter()
    code = four_view_func(img_ptrs, stage_array, w, h, pixel_size_um, C.byref(res))
    dt_ms = (time.perf_counter() - t0) * 1000.0
    return code, res, dt_ms


if __name__ == '__main__':
    eight_view_datasets = [
        (r'D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage_8', 1.8),
        (r'D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage1_8', 1.8),
        (r'D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage_1.5X_8', 3.0),
        (r'D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage_1.5X_thick_8', 3.0),
        (r'D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage_2.5X_8', 1.8),
        (r'D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage_2.5X_thick_8', 1.8),
        (r'D:\images\谷神星\标准化\EdgeImage', 1.8),
    ]

    four_view_datasets = [
        (r'D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage=4', 1.8),
        (r'D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage_1.5X', 3.0),
        (r'D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage_5X', 0.9),
        (r'D:\images\谷神星\标准化\EdgeImage', 1.8),
    ]

    print("=" * 95)
    print("WaferCalibSDK.dll Eight-View & Four-View Real Dataset Benchmark Verification")
    print("=" * 95)

    all_pass = True
    for path, pixel_size in eight_view_datasets:
        name = os.path.basename(path)
        parent = os.path.basename(os.path.dirname(path))
        label = f"{parent}\\{name}" if parent == "8视野圆弧求圆心" else name
        code, r, dt_ms = run_eight_view_dataset(path, pixel_size)
        if code == 0 and np.isfinite(r.notch_angle_deg):
            print(f"PASS 8-VIEW [{label:<36}] (px={pixel_size:.1f}um, {dt_ms:.1f}ms):")
            print(f"   Center: ({r.center_x_mm:.4f}, {r.center_y_mm:.4f}) mm | Radius: {r.radius_mm:.4f} mm")
            print(f"   RMS: {r.rms_residual_um:.2f} um | Max: {r.max_residual_um:.2f} um | Notch: {r.notch_angle_deg:.4f} deg")
        else:
            print(f"FAIL 8-VIEW [{label:<36}] Return Code: {code}, Notch: {r.notch_angle_deg}")
            all_pass = False
        print("-" * 95)

    for path, pixel_size in four_view_datasets:
        name = os.path.basename(path)
        parent = os.path.basename(os.path.dirname(path))
        label = f"{parent}\\{name}" if parent == "8视野圆弧求圆心" else name
        code, r, dt_ms = run_four_view_dataset(path, pixel_size)
        if code == 0:
            print(f"PASS 4-VIEW [{label:<36}] (px={pixel_size:.1f}um, {dt_ms:.1f}ms):")
            print(f"   Center: ({r.center_x_mm:.4f}, {r.center_y_mm:.4f}) mm | Radius: {r.radius_mm:.4f} mm")
            print(f"   RMS: {r.rms_residual_um:.2f} um | Max: {r.max_residual_um:.2f} um")
        else:
            print(f"FAIL 4-VIEW [{label:<36}] Return Code: {code}")
            all_pass = False
        print("-" * 95)

    if all_pass:
        print("ALL 11 DATASETS (7 EIGHT-VIEW + 4 FOUR-VIEW) PASSED WITH 100% SUCCESS!")
    else:
        print("SOME DATASETS FAILED!")
        raise SystemExit(1)
