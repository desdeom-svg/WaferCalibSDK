# 单张点阵畸变校正设计

## 目标

在相机、镜头、倍率和标定板姿态固定的 AOI 工位中，使用一张覆盖有效视野的规则圆点阵 Mono8 图建立可复用的几何校正模板，并用该模板将后续同尺寸 Mono8 图像重采样为等间距、正交的点阵坐标系。

## 约束

- SDK 输入和校正输出均为紧凑存储的 `CV_8UC1` / Mono8，行步长固定为 `width`。
- 标定图尺寸固定为模板的 `image_width × image_height`；尺寸不一致时拒绝校正。
- 首个样例为 4096 × 4096、10 列 × 10 行的对称圆点阵，点距为 0.15 mm；行列数和点距由调用者传入，不在算法中写死。
- 本功能建立的是当前固定视野的几何映射，不对外宣称单图可获得通用、可迁移的相机内参 `K + D`。
- 不修改既有 C API 的签名。

## 算法

1. 使用深色圆斑检测提取候选圆心；在相机和点阵板平行、网格轴近似水平/垂直的前提下，对 X/Y 投影分别聚类为调用方给定的 `(grid_columns, grid_rows)`，剔除格外污点并允许最多 10% 的缺点。每个有效圆心保留其行列编号。
2. 将圆点的物理坐标定义为 `(column × point_spacing_mm, row × point_spacing_mm)`，最小二乘拟合物理坐标到像素坐标的仿射基准。该基准生成等间距的目标圆点位置，保留当前工位的平均旋转、倍率和原点。
3. 对每个目标圆点，以归一化目标像素坐标为自变量，拟合三阶二维多项式 `target_pixel -> source_pixel`。每个坐标轴有 10 个系数：`1, x, y, x², xy, y², x³, x²y, xy², y³`。
4. 计算所有圆心的 RMS 映射残差，并作为模板质量输出。
5. 校正时对模板尺寸的每一个输出像素计算源图坐标，用 `cv::remap(..., INTER_LINEAR, BORDER_CONSTANT)` 生成同尺寸 Mono8 结果。

点阵外的图像区域仍会生成结果，但属于多项式外推区域；当前 SDK 仅负责整图校正，不对外输出计量区域放行结论。

## 接口

新增 C++ 类型 `DotGridDistortionTemplate`，仅包含：图像尺寸、实际使用圆点数、X/Y 各 10 个映射系数和 RMS 残差。

新增 C API 类型 `WaferDotGridDistortionTemplate`，字段与 C++ 模板一一对应，可由 C# `StructLayout.Sequential` 保存和传回。

```cpp
int Wafer_CreateDotGridDistortionTemplate(
    const unsigned char* calibration_image_buffer,
    int width, int height,
    int grid_columns, int grid_rows,
    double point_spacing_mm,
    WaferDotGridDistortionTemplate* out_template,
    unsigned char* result_bgr);

int Wafer_CorrectImageByDotGridTemplate(
    const unsigned char* image_buffer,
    int width, int height,
    const WaferDotGridDistortionTemplate* distortion_template,
    unsigned char* corrected_mono8);
```

第一个接口的 `result_bgr` 长度为 `width × height × 3`：绿色为检测并编号的圆心，黄色为理想网格，红色箭头为理想位置到实测位置的残差。模板中的 `detected_point_count` 和 `rms_error_pixels` 报告建模质量。第二个接口的 `corrected_mono8` 长度为 `width × height`。

## 失败与质量输出

- 空图、非 Mono8、空指针、行列数小于 2 或点距不为正：`InvalidParam` / `ImageFormatMismatch`。
- 未提取到与请求行列数一致的规则圆点阵：`FeatureNotFound`。
- 多项式求解失败或包含非有限系数：`FittingFailed`。
- 校正图尺寸与模板不一致：`ImageFormatMismatch`。
- RMS 作为量化输出而非固定阈值；上层根据量测精度要求设定放行阈值。

## 验收

- 合成的 10 × 10 径向变形圆点图能建立模板，校正后点阵的横/纵间距离散度显著低于校正前。
- C++ 与 C API 都能建立、应用模板，且 C API 输出缓冲区得到有效 Mono8 校正图。
- `images/畸变矫正/点阵.bmp` 能成功生成检测标注图与校正图，模板元数据报告实际使用圆点数与 RMS。
