import ctypes
import os
import cv2
import numpy as np

# 加载 DLL
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

print("1. 正在重新进行 5X 畸变标定...")
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

print(f"标定完成: 单视野 RMS={tpl_single.rms_error_pixels:.4f} px, 5视野 RMS={tpl_multi.rms_error_pixels:.4f} px")

# 2. 对 MyImage 和 MyImage1 执行全分辨率校正并保存
print("\n2. 正在生成全分辨率 4096x4096 矫正图像并保存...")
test_files = ["MyImage.bmp", "MyImage1.bmp"]

saved_paths = []

for fname in test_files:
    fpath = os.path.join(target_dir, fname)
    raw = cv2.imdecode(np.fromfile(fpath, dtype=np.uint8), cv2.IMREAD_GRAYSCALE)
    base_name = fname[:-4]
    
    # 单图标定矫正图像
    corr_single = np.zeros_like(raw)
    wafer_dll.Wafer_CorrectImageByDotGridTemplate(
        raw.ctypes.data_as(ctypes.c_char_p), w, h, ctypes.byref(tpl_single), corr_single.ctypes.data_as(ctypes.c_char_p)
    )
    
    # 5图多视野标定矫正图像
    corr_multi = np.zeros_like(raw)
    wafer_dll.Wafer_CorrectImageByDotGridTemplate(
        raw.ctypes.data_as(ctypes.c_char_p), w, h, ctypes.byref(tpl_multi), corr_multi.ctypes.data_as(ctypes.c_char_p)
    )
    
    # 保存到原目录 C:\Users\HYC\Desktop\畸变验证
    save_single_desktop = os.path.join(target_dir, f"{base_name}_corrected_single.bmp")
    save_multi_desktop = os.path.join(target_dir, f"{base_name}_corrected_multi.bmp")
    cv2.imencode('.bmp', corr_single)[1].tofile(save_single_desktop)
    cv2.imencode('.bmp', corr_multi)[1].tofile(save_multi_desktop)
    saved_paths.extend([save_single_desktop, save_multi_desktop])
    print(f"  已保存至桌面目录: {save_single_desktop}")
    print(f"  已保存至桌面目录: {save_multi_desktop}")
    
    # 同时在当前项目工作区也保存一份
    save_single_local = f"{base_name}_corrected_single.bmp"
    save_multi_local = f"{base_name}_corrected_multi.bmp"
    cv2.imencode('.bmp', corr_single)[1].tofile(save_single_local)
    cv2.imencode('.bmp', corr_multi)[1].tofile(save_multi_local)
    print(f"  已保存至工作区目录: {save_single_local}")
    print(f"  已保存至工作区目录: {save_multi_local}")

print("\n全部分辨率矫正图像生成完成！")
