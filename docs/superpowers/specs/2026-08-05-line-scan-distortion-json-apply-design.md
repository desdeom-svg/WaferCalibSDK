# 线扫畸变 JSON 导出与离线应用 Sample 设计

## 目标

将 `images/线扫相机畸变矫正/1.bmp` 建立的彩色线扫畸变模型持久化为 JSON 配方，并对 `images/线扫相机畸变矫正/Image_20260805110730011.bmp` 直接应用该配方，验证映射的泛化性和输入兼容性。

不重新使用新图检测黄色圆点或重新拟合任何系数。

## 范围与约束

- 不修改 WaferCalibSDK 的公开 C/C++ 接口或 ABI。
- 仅扩展现有彩色线扫 sample/test 的头文件实现与 sample 可执行文件。
- 标定与应用输入均为连续 `CV_8UC3` / BGR8；输入宽度必须精确等于标定宽度 `11008`，应用图高度允许不同于标定高度 `2000`。
- 当前离线 Sample 不记录或校验相机、镜头、倍率、安装姿态、线频、扫描速度、编码器倍率等工艺元数据；它们仅作为使用方需自行保证的复用前提，不写入 JSON，也不阻止 Sample 运行。
- 模型固定为 `line_scan_x_quadratic_y_affine_v1`：X 二次非线性，Y 仿射；不增加高阶 Y 或通用高阶 XY 系数。

## 配方格式

`line_scan_distortion.json` 使用纯文本 JSON，保存所有重建 `cv::remap` 所需数据：

```json
{
  "schema": "line_scan_x_quadratic_y_affine_v1",
  "input_width": 11008,
  "input_height": 2000,
  "input_type": "BGR8",
  "point_spacing_mm": 5.0,
  "source_x": [ax0, ax1, ax2],
  "source_y": [ay0, ay1, ay2],
  "output_origin": [origin_x, origin_y],
  "output_pitch_pixels": 498.786,
  "affine_x_rms_pixels": 2.185,
  "fitted_x_rms_pixels": 0.419,
  "y_affine_rms_pixels": 0.282
}
```

质量字段仅用于追溯；应用时实际使用标定宽度、两组系数、输出原点和输出点距。`input_height` 记录建立配方时的高度，但不作为离线应用的阻断条件。

## Sample 行为

保留 `sample_line_scan_color_distortion.exe` 的默认建模功能，并增加显式模式：

```powershell
sample_line_scan_color_distortion.exe --calibrate <calibration_bmp> <calibration_json>
sample_line_scan_color_distortion.exe --apply <calibration_json> <input_bgr_bmp> <output_bmp>
```

- `--calibrate`：读取圆点标定图，建立模型，输出 JSON 和现有诊断/校正 BMP。
- `--apply`：读取 JSON 和新 BGR 图，严格校验格式、模型版本与**图像宽度**后，直接生成与输入同高度的校正 BGR BMP；不会调用黄色圆点检测或建模函数。
- 输出使用 `cv::remap(..., BORDER_CONSTANT)`；图像右上/左下等源图范围外区域为黑色，代表无有效源像素，不代表图像内容。

## 验证

1. 序列化回归：对 `1.bmp` 建模并导出 JSON；重新加载 JSON 后应用到同一张图。结果应与内存直接校正结果逐像素一致。
2. 输入保护：错误模型版本、非 BGR 图、宽度不匹配、缺失字段或非有限系数必须失败，不生成输出；高度不同不作为失败条件。
3. 泛化验证：对 `Image_20260805110730011.bmp` 应用 `1.bmp` 的 JSON。若新图中的黄色点阵可以检测，比较校正前后的行倾斜和横向点距离散度；若不能检测，只报告已应用配方、输出尺寸和有效黑边范围，不作几何精度结论。

## 验收

- JSON 可独立保存、重启后读取，且同图回放校正结果逐像素一致。
- 新图应用成功并生成与输入相同尺寸的 BGR BMP；输入尺寸、类型和模型版本均被记录。
- 若新图可检测至少 60 个完整黄色圆点，则校正后行倾斜绝对值低于校正前，横向点距标准差不高于校正前。
- 新图不满足几何点阵验证条件时，Sample 明确输出“仅完成兼容性应用，未完成点阵几何验证”。
