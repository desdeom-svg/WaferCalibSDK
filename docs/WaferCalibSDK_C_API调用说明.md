# WaferCalibSDK C API 调用说明

本文说明 `WaferCalibSDK.dll` 的内存 C API 调用方式、接口用途与参数含义。

## 1. 通用约定

### 1.1 图像格式与缓冲区

- 所有输入图像均为 Mono8 / `CV_8UC1`，每像素 1 字节。
- 普通 Mono8 输入/输出缓冲区长度：`width * height` 字节。
- 标注结果图 `result_bgr` 的缓冲区长度：`width * height * 3` 字节，通道顺序为 **BGR**。
- 任何输出缓冲区均由调用方分配；SDK 不负责释放调用方内存。

### 1.2 返回码

| 返回值 | 宏 | 含义 |
|---:|---|---|
| 0 | `WAFER_SUCCESS` | 成功 |
| 1 | `WAFER_ERR_INVALID_PARAM` | 空指针、无效宽高、无效方法编号等 |
| 2 | `WAFER_ERR_IMAGE_EMPTY` | 输入图像为空 |
| 3 | `WAFER_ERR_FORMAT_MISMATCH` | 非 Mono8 或模板与输入尺寸不匹配 |
| 4 | `WAFER_ERR_FEATURE_NOT_FOUND` | 未找到所需 Mark、黑线或圆点阵 |
| 5 | `WAFER_ERR_FITTING_FAILED` | 几何/直线/多项式拟合失败 |
| 6 | `WAFER_ERR_FILE_IO` | 文件读写失败 |
| 999 | `WAFER_ERR_UNKNOWN` | 未分类内部错误 |

调用后必须先判断返回值是否为 `WAFER_SUCCESS`，再读取输出坐标、角度或模板。

### 1.3 C# P/Invoke 基础定义

DLL 为 C ABI。C# 建议使用 `CallingConvention.Cdecl`，并确保进程位数与 DLL 一致。

```csharp
using System;
using System.Runtime.InteropServices;

internal static class WaferCalibNative
{
    internal const int Success = 0;
    internal const int PolynomialCoefficientCount = 10;

    [StructLayout(LayoutKind.Sequential)]
    internal struct WaferDotGridDistortionTemplate
    {
        public int ImageWidth;
        public int ImageHeight;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = PolynomialCoefficientCount)]
        public double[] OutputToInputX;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = PolynomialCoefficientCount)]
        public double[] OutputToInputY;

        public int DetectedPointCount;
        public double RmsErrorPixels;
    }
}
```

`WaferDotGridDistortionTemplate` 的字段顺序不得改变，也不要使用 `Pack = 1`。首次集成时建议记录 `Marshal.SizeOf<WaferDotGridDistortionTemplate>()`，确认 C# 与部署 DLL 使用的是同一版本头文件。

### 1.4 `WaferDotGridDistortionTemplate` 定义与用途

该结构体是点阵畸变校正的**可持久化模板**：由 `Wafer_CreateDotGridDistortionTemplate` 生成，保存到配方后，再传给 `Wafer_CorrectImageByDotGridTemplate` 使用。

```c
typedef struct WaferDotGridDistortionTemplate {
    int image_width;
    int image_height;
    double output_to_input_x[10];
    double output_to_input_y[10];
    int detected_point_count;
    double rms_error_pixels;
} WaferDotGridDistortionTemplate;
```

| 字段 | 用途 | 校正时是否实际使用 |
|---|---|---|
| `image_width` | 建立模板时的图像宽度，单位像素。用于阻止将 4096 宽模板误用于其他分辨率图像。 | 是 |
| `image_height` | 建立模板时的图像高度，单位像素。 | 是 |
| `output_to_input_x[10]` | 三阶多项式的 10 个 X 映射系数：将校正后输出坐标映射回原始输入图的 X 坐标。 | 是 |
| `output_to_input_y[10]` | 三阶多项式的 10 个 Y 映射系数：将校正后输出坐标映射回原始输入图的 Y 坐标。 | 是 |
| `detected_point_count` | 实际参与模板拟合的有效圆点数，例如 99。用于建模质量记录。 | 否 |
| `rms_error_pixels` | 有效圆点的映射 RMS 残差，单位像素；数值越小，模板对本次标定点阵的拟合越好。 | 否 |

两组系数中，第 `i` 项的顺序均为：`1, x, y, x², xy, y², x³, x²y, xy², y³`；其中 `x`、`y` 为归一化后的输出像素坐标。调用方不应自行修改这些系数。

模板与以下条件绑定：图像分辨率、相机、镜头、倍率和安装姿态。更换其中任一项后必须重新建立模板。`detected_point_count` 与 `rms_error_pixels` 是质量记录，不参与 `Wafer_CorrectImageByDotGridTemplate` 的 `remap` 计算。

## 2. `Wafer_CreateDarkFrameTemplate` 与 `Wafer_CreateDarkFrameTemplateFromFiles`

### 作用

将 N 张同尺寸、同曝光、同增益的暗场 Mono8 图像合成为背景噪声模板。可用于后续图像的暗场背景扣除或均匀性处理。提供内存缓冲区版与文件路径版两种形态。

```cpp
// 方式 A：内存图像指针数组版
int Wafer_CreateDarkFrameTemplate(
    const unsigned char** frame_buffers,
    int frame_count,
    unsigned char* out_dark_template,
    int width,
    int height,
    int method);

// 方式 B：文件路径版 (支持直接落盘保存模板图像文件)
int Wafer_CreateDarkFrameTemplateFromFiles(
    const char** file_paths,
    int file_count,
    const char* save_output_path,
    int method);
```

| 参数 | 方向 | 说明 |
|---|---|---|
| `frame_buffers` | 输入 | N 个 Mono8 图像首地址组成的指针数组；每帧长度均为 `width * height`。 |
| `file_paths` | 输入 | 包含 N 个图像文件路径的字符串数组；支持包含中文的 Unicode 路径。 |
| `frame_count` | 输入 | 暗场帧数 N，必须大于 0；建议至少 5 张，通常使用 10 张。 |
| `out_dark_template` | 输出 | 调用方分配的 Mono8 输出模板，长度为 `width * height`。 |
| `save_output_path` | 输入 | 模板图像保存路径（如 `D:/calib/dark_template.bmp`）；传 NULL 或空字符串表示不落盘。 |
| `width` / `height` | 输入 | 每张暗场图的像素尺寸；所有帧必须一致。 |
| `method` | 输入 | `0`=均值，`1`=中值，`2`=3 Sigma 剔除均值。推荐 `1` 中值。 |

### C# 调用要点

```csharp
[DllImport("WaferCalibSDK.dll", CallingConvention = CallingConvention.Cdecl)]
internal static extern int Wafer_CreateDarkFrameTemplate(
    IntPtr[] frameBuffers,
    int frameCount,
    [Out] byte[] outDarkTemplate,
    int width,
    int height,
    int method);

[DllImport("WaferCalibSDK.dll", CallingConvention = CallingConvention.Cdecl)]
internal static extern int Wafer_CreateDarkFrameTemplateFromFiles(
    [In] string[] filePaths,
    int fileCount,
    [In] string saveOutputPath,
    int method);

// 方式 A: 内存数据调用 (frames 中每项均为 width * height 的 Mono8 数据)
var handles = new GCHandle[frames.Length];
var pointers = new IntPtr[frames.Length];
try
{
    for (int i = 0; i < frames.Length; ++i)
    {
        handles[i] = GCHandle.Alloc(frames[i], GCHandleType.Pinned);
        pointers[i] = handles[i].AddrOfPinnedObject();
    }

    var darkTemplate = new byte[checked(width * height)];
    int rc = Wafer_CreateDarkFrameTemplate(
        pointers, pointers.Length, darkTemplate, width, height, method: 1);
    if (rc != WaferCalibNative.Success) throw new InvalidOperationException($"暗场模板失败: {rc}");
}
finally
{
    foreach (var handle in handles)
        if (handle.IsAllocated) handle.Free();
}

// 方式 B: 文件路径直接生成
string[] darkFiles = new string[] { @"D:\calib\dark_0.bmp", @"D:\calib\dark_1.bmp" };
int fileRc = Wafer_CreateDarkFrameTemplateFromFiles(
    darkFiles, darkFiles.Length, @"D:\calib\output_dark.bmp", method: 1);
```

## 3. `Wafer_FindFourCrossMarkCenter`

### 作用

在一张 Mono8 图像中检测四个十字 Mark，分别连接左上—右下、右上—左下两个 Mark 中心，并输出两条对角线交点。用于验证晶圆/标定板中心与图像中心的偏差。

```cpp
int Wafer_FindFourCrossMarkCenter(
    const unsigned char* image_buffer,
    int width,
    int height,
    double* calculated_center_x,
    double* calculated_center_y,
    unsigned char* result_bgr);
```

| 参数 | 方向 | 说明 |
|---|---|---|
| `image_buffer` | 输入 | Mono8 图像，长度为 `width * height`。 |
| `width` / `height` | 输入 | 图像尺寸。 |
| `calculated_center_x` | 输出 | 四点对角线交点的 X 坐标，单位为像素。 |
| `calculated_center_y` | 输出 | 四点对角线交点的 Y 坐标，单位为像素。 |
| `result_bgr` | 输出 | BGR 标注图，长度为 `width * height * 3`；包含检测到的 Mark 中心及最终中心。 |

### C# 调用示例

```csharp
[DllImport("WaferCalibSDK.dll", CallingConvention = CallingConvention.Cdecl)]
internal static extern int Wafer_FindFourCrossMarkCenter(
    [In] byte[] imageBuffer,
    int width,
    int height,
    out double calculatedCenterX,
    out double calculatedCenterY,
    [Out] byte[] resultBgr);

var resultBgr = new byte[checked(width * height * 3)];
int rc = Wafer_FindFourCrossMarkCenter(
    mono8, width, height, out double centerX, out double centerY, resultBgr);
if (rc != WaferCalibNative.Success) throw new InvalidOperationException($"四 Mark 检测失败: {rc}");
```

## 4. `Wafer_FindHorizontalLineAngle`

### 作用

检测图像中的长黑色水平标定线，输出与图像 X 轴的夹角。用于验证运动轴与相机坐标轴的正交性。

```cpp
int Wafer_FindHorizontalLineAngle(
    const unsigned char* image_buffer,
    int width,
    int height,
    double* angle_degrees,
    unsigned char* result_bgr);
```

| 参数 | 方向 | 说明 |
|---|---|---|
| `image_buffer` | 输入 | Mono8 图像，长度为 `width * height`。 |
| `width` / `height` | 输入 | 图像尺寸。 |
| `angle_degrees` | 输出 | 角度范围 `[-90, 90)`；从左向右向下倾斜为正，理想水平线为 `0°`。 |
| `result_bgr` | 输出 | BGR 标注图，长度为 `width * height * 3`；绿色为有效候选线，黄色为融合结果，红框为触及上/下边界而被剔除的线。 |

只有完整的水平标定线参与角度融合；横向贯穿左右视野的线允许参与，触及上/下边界的截断线会被排除。

```csharp
[DllImport("WaferCalibSDK.dll", CallingConvention = CallingConvention.Cdecl)]
internal static extern int Wafer_FindHorizontalLineAngle(
    [In] byte[] imageBuffer,
    int width,
    int height,
    out double angleDegrees,
    [Out] byte[] resultBgr);
```

## 5. `Wafer_CreateDotGridDistortionTemplate`

### 作用

从一张规则圆点阵 Mono8 图建立固定视野的几何校正模板。适用于相机、镜头、倍率和标定板姿态固定的工位；它不是通用的多视角相机内参标定。

```cpp
int Wafer_CreateDotGridDistortionTemplate(
    const unsigned char* calibration_image_buffer,
    int width,
    int height,
    int grid_columns,
    int grid_rows,
    double point_spacing_mm,
    WaferDotGridDistortionTemplate* out_template,
    unsigned char* result_bgr);
```

| 参数 | 方向 | 说明 |
|---|---|---|
| `calibration_image_buffer` | 输入 | 圆点阵 Mono8 图像，长度为 `width * height`。 |
| `width` / `height` | 输入 | 标定图尺寸。后续校正图必须使用同一尺寸。 |
| `grid_columns` | 输入 | 期望圆点阵列数，例如 `10`。 |
| `grid_rows` | 输入 | 期望圆点阵行数，例如 `10`。 |
| `point_spacing_mm` | 输入 | 相邻圆点实际间距，单位 mm，例如 `0.15`。 |
| `out_template` | 输出 | 可保存和复用的畸变校正模板。 |
| `result_bgr` | 输出 | BGR 诊断图，长度为 `width * height * 3`；显示圆心编号、理想网格、残差和 RMS。 |

模板中的 `detected_point_count` 表示实际参与拟合的圆点数；`rms_error_pixels` 越小表示圆心映射拟合越好。当前算法允许少量缺点并剔除网格外污点。

### C# 声明与调用示例

```csharp
[DllImport("WaferCalibSDK.dll", CallingConvention = CallingConvention.Cdecl)]
internal static extern int Wafer_CreateDotGridDistortionTemplate(
    [In] byte[] calibrationImageBuffer,
    int width,
    int height,
    int gridColumns,
    int gridRows,
    double pointSpacingMm,
    ref WaferCalibNative.WaferDotGridDistortionTemplate outTemplate,
    [Out] byte[] resultBgr);

var template = new WaferCalibNative.WaferDotGridDistortionTemplate
{
    OutputToInputX = new double[WaferCalibNative.PolynomialCoefficientCount],
    OutputToInputY = new double[WaferCalibNative.PolynomialCoefficientCount]
};
var diagnosticBgr = new byte[checked(width * height * 3)];
int rc = Wafer_CreateDotGridDistortionTemplate(
    dotGridMono8, width, height,
    gridColumns: 10, gridRows: 10, pointSpacingMm: 0.15,
    ref template, diagnosticBgr);
if (rc != WaferCalibNative.Success) throw new InvalidOperationException($"建立畸变模板失败: {rc}");

Console.WriteLine($"有效圆点={template.DetectedPointCount}, RMS={template.RmsErrorPixels:F4} px");
```

## 6. `Wafer_CreateMultiViewDotGridDistortionTemplate` 与 `Wafer_CreateMultiViewDotGridTemplateFromFiles`

### 作用与背景

在显微物镜大画幅成像系统中，当标定板物理尺寸有限无法一次性覆盖相机整个视场（尤其是视场边缘区域极易缺失标定特征点）时，采用 **5 视野平移联合标定方案**。

在相机位置保持绝对静止的前提下，通过载物平台分别移动至 **[中心、左上、左下、右上、右下]** 5 个点位进行采图。算法基于各视野中心特征定位锚点（第 101 个圆，其到 4 个对角邻点距离为 $Pitch / \sqrt{2}$）自动锁定物理网格的行列拓扑，并通过重叠区域自适应约束各视野间刚体相对位姿，最终进行全像面二维三阶双多项式 Huber 鲁棒联合优化，生成全局唯一的全画幅高精度畸变标定模板。

### 标定板规格与推荐参数表

| 物镜倍率 | 标定板网格 | 大圆间距 (Pitch) | 推荐机台步长 (Step) | 典型全像面 RMS 残差 |
| :---: | :---: | :---: | :---: | :---: |
| **1.5X** | 10 × 10 点阵 | $1.00\,\text{mm}$ | $1.50\,\text{mm}$ | $\approx 0.21\,\text{px}$ |
| **2.5X** | 10 × 10 点阵 | $0.60\,\text{mm}$ | $0.90\,\text{mm}$ | $\approx 0.23\,\text{px}$ |
| **5X** | 10 × 10 点阵 | $0.30\,\text{mm}$ | $0.45\,\text{mm}$ | $\approx 0.18\,\text{px}$ |
| **10X** | 10 × 10 点阵 | $0.15\,\text{mm}$ | $0.22\,\text{mm}$ | $\approx 0.21\,\text{px}$ |
| **20X** | 10 × 10 点阵 | $0.07\,\text{mm}$ | $0.135\,\text{mm}$ | $\approx 0.77\,\text{px}$ |

### 接口定义 (C ABI)

```cpp
// 方式 A：内存图像缓冲区版 (适用于上位机采集卡实时连续取图后直接标定)
int Wafer_CreateMultiViewDotGridDistortionTemplate(
    const unsigned char** image_buffers,
    int width,
    int height,
    int grid_columns,
    int grid_rows,
    double point_spacing_mm,
    double stage_step_mm,
    WaferDotGridDistortionTemplate* out_template,
    unsigned char* result_bgr);

// 方式 B：文件路径版 (适用于离线批量标定，支持直接生成 3x3 无损拼接大图)
int Wafer_CreateMultiViewDotGridTemplateFromFiles(
    const char** file_paths,
    int grid_columns,
    int grid_rows,
    double point_spacing_mm,
    double stage_step_mm,
    WaferDotGridDistortionTemplate* out_template,
    const char* save_diagnostic_image_path);
```

### 参数说明

| 参数 | 方向 | 说明 |
|---|---|---|
| `image_buffers` | 输入 | 长度为 5 的指针数组，每个指针指向单张 Mono8 图像内存（长度 `width * height`）。**图像顺序必须严格固定为：`[0]中心, [1]左上, [2]左下, [3]右上, [4]右下`**。 |
| `file_paths` | 输入 | 长度为 5 的字符串指针数组，分别对应 5 个视野图像文件路径。顺序同上。支持包含中文字符的 Unicode 路径。 |
| `width` / `height` | 输入 | 单张视野图像的宽高（像素），5 个视野的图像尺寸必须一致（如 4096 × 4096）。 |
| `grid_columns` / `grid_rows` | 输入 | 标定板网格阵列规格，通常为 `10` 列 × `10` 行。 |
| `point_spacing_mm` | 输入 | 标定板大圆物理中心间距（mm），按物镜倍率查上表传入。 |
| `stage_step_mm` | 输入 | 机台平移步长先验值（mm）；若传 `0.0` 则算法采用纯视觉自适应特征匹配对齐。 |
| `out_template` | 输出 | 导出的全局单一标定模板结构体指针。 |
| `result_bgr` | 输出 | 内存版诊断图缓冲区指针（由调用方分配 `width * height * 3` 字节）。若传入 NULL 则不生成。 |
| `save_diagnostic_image_path` | 输入 | 文件版诊断大图保存路径（如 `output_diagnostic.png`），自动生成 $12288 \times 12288$ 无损 3x3 物理拼接全景诊断图。传 NULL 或空串表示不落盘。 |

### 3x3 十字物理拓扑无损拼接诊断大图说明

当传入 `save_diagnostic_image_path` 时，算法自动生成一张 $12288 \times 12288$ 像素的无损全景诊断图（由 9 个 $4096 \times 4096$ 原生分辨率子图合成）：

1. **各视野完全独立，绝无混画与悬空点**：
   - 5 个视野分别位于 3x3 网格的对应物理方位（中心在 `(1,1)`，左上在 `(0,0)`，右上在 `(2,0)`，左下在 `(0,2)`，右下在 `(2,2)`）；
   - 每个视野子图底图均为**该视野自身拍摄的原始真实图像**，各点位只绘制属于自己检出的圆点；
   - 圆点包含三层结构：彩色外圈加十字（观测值）、白色内圈（理论值）、红色小箭头（残差矢量）；
   - 中心特征定位锚点（第 101 个圆）标记专属金色线框与 `Anchor #101` 标签；
   - 子图顶部横幅标出点位名称、检出有效点数及该子图局部 RMS 残差。
2. **全局看板与图例**：
   - 上方区域 `(1,0)`：全局联合标定指标看板（分辨率、点距、步长、总内点数、全局 RMS、判定结果）；
   - 下方区域 `(1,2)`：图例说明与二维三阶双多项式数学模型规格；
   - 左右区域 `(0,1)` 与 `(2,1)`：标出机台相对于中心视野的平移运动矢量与方向指示。

### C# P/Invoke 声明与调用示例

```csharp
// 声明 A：内存图像指针数组版
[DllImport("WaferCalibSDK.dll", CallingConvention = CallingConvention.Cdecl)]
internal static extern int Wafer_CreateMultiViewDotGridDistortionTemplate(
    [In] IntPtr[] imageBuffers,
    int width,
    int height,
    int gridColumns,
    int gridRows,
    double pointSpacingMm,
    double stageStepMm,
    ref WaferCalibNative.WaferDotGridDistortionTemplate outTemplate,
    [Out] byte[] resultBgr);

// 声明 B：文件路径版 (推荐)
[DllImport("WaferCalibSDK.dll", CallingConvention = CallingConvention.Cdecl)]
internal static extern int Wafer_CreateMultiViewDotGridTemplateFromFiles(
    [In] string[] filePaths,
    int gridColumns,
    int gridRows,
    double pointSpacingMm,
    double stageStepMm,
    ref WaferCalibNative.WaferDotGridDistortionTemplate outTemplate,
    [In] string saveDiagnosticImagePath);

// ---------------------------------------------------------------
// 调用示例 1：通过文件路径直接标定并生成 3x3 拼接诊断图
// ---------------------------------------------------------------
string[] views = new string[5]
{
    @"D:\images\谷神星\标准化\畸变矫正\1.5XImages\中心.bmp",
    @"D:\images\谷神星\标准化\畸变矫正\1.5XImages\左上.bmp",
    @"D:\images\谷神星\标准化\畸变矫正\1.5XImages\左下.bmp",
    @"D:\images\谷神星\标准化\畸变矫正\1.5XImages\右上.bmp",
    @"D:\images\谷神星\标准化\畸变矫正\1.5XImages\右下.bmp"
};

var template = new WaferCalibNative.WaferDotGridDistortionTemplate
{
    OutputToInputX = new double[WaferCalibNative.PolynomialCoefficientCount],
    OutputToInputY = new double[WaferCalibNative.PolynomialCoefficientCount]
};

string diagnosticPath = @"D:\images\谷神星\标准化\畸变矫正\1.5XImages\output_diagnostic.png";

// 执行 1.5X 倍率 5 视野联合标定 (pitch=1.0mm, step=1.5mm)
int ret = Wafer_CreateMultiViewDotGridTemplateFromFiles(
    views,
    gridColumns: 10,
    gridRows: 10,
    pointSpacingMm: 1.0,
    stageStepMm: 1.5,
    ref template,
    diagnosticPath);

if (ret != WaferCalibNative.Success)
{
    throw new InvalidOperationException($"5 视野联合标定失败，错误码: {ret}");
}

Console.WriteLine($"[标定成功] 参与拟合点数: {template.DetectedPointCount}, 全局 RMS: {template.RmsErrorPixels:F4} px");

// ---------------------------------------------------------------
// 调用示例 2：内存数据调用 (从相机/采集卡直接获取 5 帧 Mono8 连续缓存)
// ---------------------------------------------------------------
// byte[][] fovBuffers 顺序：[0]中心, [1]左上, [2]左下, [3]右上, [4]右下
var handles = new GCHandle[5];
var ptrs = new IntPtr[5];
try
{
    for (int i = 0; i < 5; ++i)
    {
        handles[i] = GCHandle.Alloc(fovBuffers[i], GCHandleType.Pinned);
        ptrs[i] = handles[i].AddrOfPinnedObject();
    }

    var liveTemplate = new WaferCalibNative.WaferDotGridDistortionTemplate
    {
        OutputToInputX = new double[WaferCalibNative.PolynomialCoefficientCount],
        OutputToInputY = new double[WaferCalibNative.PolynomialCoefficientCount]
    };
    // 可选分配内存诊断图 (4096 * 4096 * 3)，传 null 表示不输出
    byte[] diagBuf = new byte[width * height * 3];

    int rc = Wafer_CreateMultiViewDotGridDistortionTemplate(
        ptrs, width, height, 10, 10, 1.0, 1.5, ref liveTemplate, diagBuf);
    if (rc != WaferCalibNative.Success) throw new InvalidOperationException($"在线联合标定失败: {rc}");
}
finally
{
    for (int i = 0; i < 5; ++i)
    {
        if (handles[i].IsAllocated) handles[i].Free();
    }
}
```

## 7. `Wafer_SaveDistortionTemplateToFile` 与 `Wafer_LoadDistortionTemplateFromFile`

### 作用

用于将建立好的 `WaferDotGridDistortionTemplate` 标定模板持久化保存到磁盘，或在生产检测程序启动时高速载入。

SDK 原生支持两种存储格式，根据文件扩展名自动适配：
1. **`.json` 格式**：人类可读的明文 JSON，包含图像尺寸、检出点数、RMS 残差以及 20 个三阶双多项式系数，方便研发调试、版本核验与工艺归档。
2. **`.bin` 或 `.dat` 格式**：带 `WFRCALIB` 专用幻数头的紧凑型二进制存储格式，体积小、读取极快（$< 1\,\text{ms}$），适用于机台生产配方部署。

### 接口定义 (C ABI)

```cpp
// 将模板保存到文件 (根据扩展名自动选择 JSON 或 BIN)
int Wafer_SaveDistortionTemplateToFile(
    const char* file_path,
    const WaferDotGridDistortionTemplate* distortion_template);

// 从文件载入模板 (自动识别 JSON 或 BIN 格式)
int Wafer_LoadDistortionTemplateFromFile(
    const char* file_path,
    WaferDotGridDistortionTemplate* distortion_template);
```

### 参数说明

| 参数 | 方向 | 说明 |
|---|---|---|
| `file_path` | 输入 | 目标文件路径（支持 Windows Unicode 中文路径）。扩展名为 `.json` 保存为文本，`.bin` 或 `.dat` 保存为高效二进制。 |
| `distortion_template` | 输入 / 输出 | 待保存的模板指针 / 待写入的模板结构体接收指针。 |

### C# P/Invoke 声明与调用示例

```csharp
[DllImport("WaferCalibSDK.dll", CallingConvention = CallingConvention.Cdecl)]
internal static extern int Wafer_SaveDistortionTemplateToFile(
    [In] string filePath,
    ref WaferCalibNative.WaferDotGridDistortionTemplate distortionTemplate);

[DllImport("WaferCalibSDK.dll", CallingConvention = CallingConvention.Cdecl)]
internal static extern int Wafer_LoadDistortionTemplateFromFile(
    [In] string filePath,
    ref WaferCalibNative.WaferDotGridDistortionTemplate distortionTemplate);

// 保存模板 (以 JSON 为例)
Wafer_SaveDistortionTemplateToFile(@"D:\calibration\1.5X_template.json", ref template);
// 保存模板 (以 BIN 为例)
Wafer_SaveDistortionTemplateToFile(@"D:\calibration\1.5X_template.bin", ref template);

// 在生产流程中高速加载模板
var loadedTemplate = new WaferCalibNative.WaferDotGridDistortionTemplate
{
    OutputToInputX = new double[WaferCalibNative.PolynomialCoefficientCount],
    OutputToInputY = new double[WaferCalibNative.PolynomialCoefficientCount]
};
int loadRet = Wafer_LoadDistortionTemplateFromFile(@"D:\calibration\1.5X_template.bin", ref loadedTemplate);
if (loadRet == WaferCalibNative.Success)
{
    Console.WriteLine($"模板加载成功: {loadedTemplate.ImageWidth}x{loadedTemplate.ImageHeight}, RMS={loadedTemplate.RmsErrorPixels:F4} px");
}
```

## 8. `Wafer_CorrectImageByDotGridTemplate`

### 作用

使用已建立的点阵畸变模板，对一张同尺寸 Mono8 图进行整图几何校正。适用于由单视野标定（第 5 节）或 5 视野联合标定（第 6 节）生成的任意 `WaferDotGridDistortionTemplate` 模板。

```cpp
int Wafer_CorrectImageByDotGridTemplate(
    const unsigned char* image_buffer,
    int width,
    int height,
    const WaferDotGridDistortionTemplate* distortion_template,
    unsigned char* corrected_mono8);
```

| 参数 | 方向 | 说明 |
|---|---|---|
| `image_buffer` | 输入 | 待校正 Mono8 图像，长度为 `width * height`。 |
| `width` / `height` | 输入 | 输入图尺寸，必须等于模板中的 `ImageWidth` / `ImageHeight`。 |
| `distortion_template` | 输入 | 由标定接口成功返回的模板，或由 `Wafer_LoadDistortionTemplateFromFile` 从配方恢复的数据。 |
| `corrected_mono8` | 输出 | 调用方分配的 Mono8 输出缓冲区，长度为 `width * height`。 |

```csharp
[DllImport("WaferCalibSDK.dll", CallingConvention = CallingConvention.Cdecl)]
internal static extern int Wafer_CorrectImageByDotGridTemplate(
    [In] byte[] imageBuffer,
    int width,
    int height,
    ref WaferCalibNative.WaferDotGridDistortionTemplate distortionTemplate,
    [Out] byte[] correctedMono8);

var correctedMono8 = new byte[checked(width * height)];
int rc = Wafer_CorrectImageByDotGridTemplate(
    inputMono8, width, height, ref template, correctedMono8);
if (rc != WaferCalibNative.Success) throw new InvalidOperationException($"畸变校正失败: {rc}");
```

## 9. `Wafer_FindCircleCenterOffset`

### 作用

在 Mono8 图像中检测圆形目标，计算圆中心与图像几何中心（`width / 2.0`, `height / 2.0`）的差值（$\Delta X, \Delta Y$），并生成可视化诊断标注图（标记圆轮廓、拟合圆、圆中心、图像中心及差值信息）。

算法内置多尺度高斯差分（DoG）与自适应动态分位数阈值分割，能够在不同曝光亮度、低对比度、光照不均匀及颗粒噪声干扰下稳定提取圆目标。

```cpp
int Wafer_FindCircleCenterOffset(
    const unsigned char* image_buffer,
    int width,
    int height,
    double* offset_x,
    double* offset_y,
    unsigned char* result_bgr);
```

### 参数说明

| 参数 | 方向 | 说明 |
|---|---|---|
| `image_buffer` | 输入 | 输入 Mono8 图像缓冲区，长度为 `width * height` 字节。 |
| `width` / `height` | 输入 | 图像宽度与高度，单位像素。 |
| `offset_x` | 输出 | **必填**。圆中心与图像几何中心的 X 方向差值（`circle_center_x - image_center_x`），单位像素。 |
| `offset_y` | 输出 | **必填**。圆中心与图像几何中心的 Y 方向差值（`circle_center_y - image_center_y`），单位像素。 |
| `result_bgr` | 输出 | **必填**。诊断标注图缓冲区（BGR888），由调用方分配 `width * height * 3` 字节。 |

### 诊断标注图图例说明

- **图像中心**：红色十字标记 `ImgCenter(Ix, Iy)`，图像中心定义为 `width / 2.0, height / 2.0`（例如 2048x2048 图像对应 1024.0, 1024.0）。
- **圆中心**：绿色十字标记 `CircleCenter(Cx, Cy)`。
- **圆轮廓与拟合圆**：黄色多边形标记真实检测轮廓，青色圆环标记最小二乘代数拟合圆。
- **中心连线**：洋红色实线连接图像中心与圆中心。
- **左上角信息面板**：显示偏移量 `dx`, `dy` 及拟合半径 `R`、圆度 `Circ`、残差 `RMS`。

### C# 声明与调用示例

```csharp
[DllImport("WaferCalibSDK.dll", CallingConvention = CallingConvention.Cdecl)]
internal static extern int Wafer_FindCircleCenterOffset(
    [In] byte[] imageBuffer,
    int width,
    int height,
    out double offsetX,
    out double offsetY,
    [Out] byte[] resultBgr);

// 调用示例
var resultBgr = new byte[checked(width * height * 3)];
int rc = Wafer_FindCircleCenterOffset(
    imageMono8,
    width,
    height,
    out double dx,
    out double dy,
    resultBgr);

if (rc != WaferCalibNative.Success)
{
    throw new InvalidOperationException($"求圆中心与图像中心差值失败，错误码: {rc}");
}

double imageCenterX = width / 2.0;
double imageCenterY = height / 2.0;
double circleCenterX = imageCenterX + dx;
double circleCenterY = imageCenterY + dy;

Console.WriteLine($"图像中心: ({imageCenterX:F1}, {imageCenterY:F1})");
Console.WriteLine($"圆中心: ({circleCenterX:F3}, {circleCenterY:F3})");
Console.WriteLine($"中心差值: dx={dx:F3} px, dy={dy:F3} px");
```

### 一键批量生成 48 张图结果说明

在项目根目录下提供了两种一键批量运行方式：
1. **运行脚本**：直接双击项目根目录下的 [`run_generate_48_results.bat`](file:///d:/Projects/opencvProject/WaferCalibSDK/run_generate_48_results.bat)；
2. **命令行运行**：
   ```cmd
   .\build\Release\sample_circle_center_offset.exe
   ```
程序将自动扫描 `images/求圆中心与图像中心的差值/20260822_0/` 下的所有 48 张原图（`324.bmp` ~ `371.bmp`），对每张图计算中心差值并将 48 张诊断标注图统一保存至子目录：
`images/求圆中心与图像中心的差值/20260822_0/results/`（如 `324_result.bmp` ~ `371_result.bmp`）。

---

## 10. C++ 原生模块接口与数据结构参考

对于直接使用 C++17 开发的上位机、算法服务或嵌入式视觉程序，可直接包含主头文件 `#include "wafer_calib/wafer_calib.hpp"` 调用现代 C++ API。

### 10.1 核心数据结构与枚举一览

#### 1. `wafer_calib::Point2D` (亚像素二维点)
头文件：`#include "wafer_calib/core/types.hpp"`
```cpp
struct Point2D {
    double x = 0.0;
    double y = 0.0;
    Point2D() = default;
    Point2D(double px, double py) : x(px), y(py) {}
    Point2D(const cv::Point2f& pt) : x(pt.x), y(pt.y) {}
    Point2D(const cv::Point2d& pt) : x(pt.x), y(pt.y) {}
    cv::Point2d toCvPoint() const { return cv::Point2d(x, y); }
};
```

#### 2. `wafer_calib::CircleDetectionResult` (圆检测与中心差值结果)
头文件：`#include "wafer_calib/modules/circle_center_offset.hpp"`
```cpp
struct CircleDetectionResult {
    Point2D circle_center;      // 拟合求得的圆中心亚像素像面坐标 (x, y)
    double radius = 0.0;        // 拟合圆半径，单位：像素
    Point2D image_center;       // 图像几何中心坐标 (width / 2.0, height / 2.0)
    double offset_x = 0.0;      // X 方向差值 (circle_center.x - image_center.x)，单位：像素
    double offset_y = 0.0;      // Y 方向差值 (circle_center.y - image_center.y)，单位：像素
    double circularity = 0.0;   // 轮廓圆度 (4 * pi * area / perimeter^2)
    double rms_residual = 0.0;  // 轮廓采样点到拟合圆周的 RMS 几何残差，单位：像素
};
```

#### 3. `wafer_calib::DotGridDistortionTemplate` (畸变校正模板)
头文件：`#include "wafer_calib/modules/distortion_correction.hpp"`
```cpp
constexpr int kDotGridPolynomialCoefficientCount = 10;

struct DotGridDistortionTemplate {
    int image_width = 0;                                                      // 标定图像宽度 (像素)
    int image_height = 0;                                                     // 标定图像高度 (像素)
    std::array<double, kDotGridPolynomialCoefficientCount> output_to_input_x{}; // X 方向 10 项多项式映射系数
    std::array<double, kDotGridPolynomialCoefficientCount> output_to_input_y{}; // Y 方向 10 项多项式映射系数
    int detected_point_count = 0;                                             // 参与拟合的有效圆点数
    double rms_error_pixels = 0.0;                                            // 全局加权映射 RMS 残差 (像素)
};
```

#### 4. `wafer_calib::CalibrationViewPosition` 与 `CalibrationViewInput` (多视野输入)
头文件：`#include "wafer_calib/modules/distortion_correction.hpp"`
```cpp
enum class CalibrationViewPosition {
    Center = 0,     // 中心视野
    TopLeft = 1,    // 左上视野
    BottomLeft = 2, // 左下视野
    TopRight = 3,   // 右上视野
    BottomRight = 4 // 右下视野
};

struct CalibrationViewInput {
    CalibrationViewPosition position = CalibrationViewPosition::Center; // 拍摄方位点位
    cv::Mat image_mono8;                                                // 该视野捕获的 Mono8 图像
    double stage_offset_x_mm = 0.0;                                     // 机台 X 方向位移 (mm，可选)
    double stage_offset_y_mm = 0.0;                                     // 机台 Y 方向位移 (mm，可选)
};
```

#### 5. `wafer_calib::FrameCombineMethod` (多帧暗场融合策略)
头文件：`#include "wafer_calib/modules/noise_reduction.hpp"`
```cpp
enum class FrameCombineMethod {
    Mean = 0,      // 均值融合
    Median = 1,    // 中值融合 (暗场去热点/噪点推荐)
    SigmaClip = 2  // 3-Sigma 离群剔除均值融合
};
```

#### 6. `wafer_calib::ErrorCode` 与 `Status` (统一错误处理类)
头文件：`#include "wafer_calib/core/status.hpp"`
```cpp
enum class ErrorCode {
    Success = 0,               // 操作成功
    InvalidParam = 1,          // 无效输入参数
    ImageEmpty = 2,            // 图像为空或无法读取
    ImageFormatMismatch = 3,   // 图像格式/通道/尺寸不匹配
    FeatureNotFound = 4,       // 未能提取到足够特征（点阵、Mark点、直线）
    FittingFailed = 5,         // 几何或多项式拟合失败
    FileIOError = 6,           // 文件打开或读写错误
    UnknownError = 999         // 未分类错误
};

struct Status {
    ErrorCode code = ErrorCode::Success;
    std::string message;
    bool ok() const;
    static Status OK();
    static Status Error(ErrorCode err_code, const std::string& msg);
};
```

### 10.2 Unicode 中文字符路径图像安全读写辅助函数
头文件：`#include "wafer_calib/core/types.hpp"`

针对 Windows 平台上 OpenCV 默认 `cv::imread` / `cv::imwrite` 不支持中文路径的固有缺陷，SDK 提供了直接兼容 UTF-8 / ANSI 双编码的安全读写函数：
```cpp
// 读取支持中文字符路径的图像 (底层使用二进制流 + cv::imdecode)
cv::Mat readImageUnicode(const std::string& filepath, int flags = cv::IMREAD_COLOR);

// 写入支持中文字符路径的图像 (底层使用 cv::imencode + 二进制文件写入)
bool writeImageUnicode(const std::string& filepath, const cv::Mat& image);
```

### 10.3 C++ 模块类与核心静态函数一览

| 模块类名 | 头文件 | 核心成员函数 | 功能说明 |
|---|---|---|---|
| `CircleCenterOffsetModule` | `circle_center_offset.hpp` | `findCircleCenterOffset(mono8, result, result_bgr)` | 亚像素提取圆目标并求解与图像中心的偏移 |
| `MarkCenterModule` | `mark_center.hpp` | `findFourCrossMarkCenter(mono8, center, result_bgr)` | 检测四个十字 Mark 并计算双对角线几何交点 |
| `LineAngleModule` | `line_angle.hpp` | `findHorizontalLineAngle(mono8, angle, result_bgr)` | 亚像素拟合黑色标定横线并测量微弱倾角 |
| `DistortionCorrectionModule` | `distortion_correction.hpp` | `createDotGridTemplate(...)` | 单视野规则圆点阵畸变标定 |
| | | `createMultiViewDotGridTemplate(...)` | 5 视野联合大画幅畸变标定与 3x3 诊断大图生成 |
| | | `correctByDotGridTemplate(...)` | 使用标定模板对单幅 Mono8 图进行几何去畸变 |
| | | `saveTemplateToFile(path, template)` | 将标定模板序列化保存为 `.json` 或 `.bin` 文件 |
| | | `loadTemplateFromFile(path, template)` | 从磁盘加载 `.json` 或 `.bin` 标定模板 |
| `NoiseReductionModule` | `noise_reduction.hpp` | `createDarkFrameTemplate(frames, out_tpl, method)` | 多帧暗场背景去噪模板合成 |

### 10.4 C++ 原生端到端开发示例

```cpp
#include <iostream>
#include <vector>
#include "wafer_calib/wafer_calib.hpp"

int main() {
    using namespace wafer_calib;

    // 1. 准备 5 视野输入 (支持中文路径)
    const std::vector<std::string> view_files = {
        "D:/images/1.5XImages/中心.bmp",
        "D:/images/1.5XImages/左上.bmp",
        "D:/images/1.5XImages/左下.bmp",
        "D:/images/1.5XImages/右上.bmp",
        "D:/images/1.5XImages/右下.bmp"
    };

    const CalibrationViewPosition positions[5] = {
        CalibrationViewPosition::Center,
        CalibrationViewPosition::TopLeft,
        CalibrationViewPosition::BottomLeft,
        CalibrationViewPosition::TopRight,
        CalibrationViewPosition::BottomRight
    };

    std::vector<CalibrationViewInput> views(5);
    for (int i = 0; i < 5; ++i) {
        views[i].position = positions[i];
        views[i].image_mono8 = readImageUnicode(view_files[i], cv::IMREAD_GRAYSCALE);
    }

    // 2. 执行 5 视野联合标定
    DotGridDistortionTemplate distortion_template;
    cv::Mat diagnostic_stitched_12k;

    Status status = DistortionCorrectionModule::createMultiViewDotGridTemplate(
        views,
        10, 10,       // 10x10 网格
        1.0,          // 物理点间距 1.0 mm (1.5X)
        1.5,          // 推荐机台平移步长 1.5 mm
        distortion_template,
        diagnostic_stitched_12k // 输出 12288x12288 3x3 无损拼接大图
    );

    if (!status.ok()) {
        std::cerr << "标定失败: " << status.message << std::endl;
        return -1;
    }

    std::cout << "标定成功! 有效内点数=" << distortion_template.detected_point_count
              << ", 全局加权 RMS=" << distortion_template.rms_error_pixels << " px" << std::endl;

    // 3. 保存 3x3 拼接诊断大图与标定文件
    writeImageUnicode("D:/images/1.5XImages/output_diagnostic.png", diagnostic_stitched_12k);
    DistortionCorrectionModule::saveTemplateToFile("D:/images/1.5XImages/template.json", distortion_template);
    DistortionCorrectionModule::saveTemplateToFile("D:/images/1.5XImages/template.bin", distortion_template);

    // 4. 实时校正单张图像
    cv::Mat raw_image = readImageUnicode("D:/images/1.5XImages/中心.bmp", cv::IMREAD_GRAYSCALE);
    cv::Mat corrected_image;
    DistortionCorrectionModule::correctByDotGridTemplate(raw_image, distortion_template, corrected_image);
    writeImageUnicode("D:/images/1.5XImages/output_corrected.bmp", corrected_image);

    return 0;
}
```



