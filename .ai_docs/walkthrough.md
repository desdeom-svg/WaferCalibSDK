# 圆中心与图像中心差值计算功能交付报告（精简版 API）

## 1. 变更总结

根据反馈进行了以下两项优化：
1. **精简 C API 参数**：移除了非必需的可选参数，仅保留最核心的必要输入与输出参数，与 SDK 中其他接口（如 `Wafer_FindHorizontalLineAngle`）的简洁设计风格完全统一；
2. **支持一键批量生成 48 张图结果**：
   - 编写了批量执行程序与一键启动脚本 [`run_generate_48_results.bat`](file:///d:/Projects/opencvProject/WaferCalibSDK/run_generate_48_results.bat)；
   - 自动遍历 `images/求圆中心与图像中心的差值/20260822_0/` 下全部 48 张原图，并将 48 张带标注的结果图（`324_result.bmp` ~ `371_result.bmp`）输出至子目录 `results/` 下。

---

## 2. 精简后的 C API 规范

```c
/**
 * @brief 在 Mono8 图像中检测圆形目标，计算圆中心与图像几何中心的差值 (dx, dy)，并生成诊断标注图。
 * @param image_buffer 输入 Mono8 图像缓冲区，长度为 width * height 字节。
 * @param width 图像宽度，单位：像素。
 * @param height 图像高度，单位：像素。
 * @param offset_x 输出圆中心与图像中心的 X 差值 (circle_center_x - image_center_x)，单位：像素。
 * @param offset_y 输出圆中心与图像中心的 Y 差值 (circle_center_y - image_center_y)，单位：像素。
 * @param result_bgr 输出 BGR 诊断标注图缓冲区，由调用方分配 width * height * 3 字节。
 * @return WAFER_SUCCESS 或 WAFER_ERR_* 错误码。
 */
WAFER_API int Wafer_FindCircleCenterOffset(
    const unsigned char* image_buffer,
    int width,
    int height,
    double* offset_x,
    double* offset_y,
    unsigned char* result_bgr
);
```

### C# P/Invoke 声明
```csharp
[DllImport("WaferCalibSDK.dll", CallingConvention = CallingConvention.Cdecl)]
internal static extern int Wafer_FindCircleCenterOffset(
    [In] byte[] imageBuffer,
    int width,
    int height,
    out double offsetX,
    out double offsetY,
    [Out] byte[] resultBgr);
```

---

## 3. 一键生成 48 张图结果图的操作方式

### 方式 A（推荐）：双击根目录批处理脚本
在工程根目录直接双击运行：
👉 [`run_generate_48_results.bat`](file:///d:/Projects/opencvProject/WaferCalibSDK/run_generate_48_results.bat)

### 方式 B：终端命令行运行
在项目根目录下执行：
```powershell
.\build\Release\sample_circle_center_offset.exe
```

执行后将自动完成以下处理：
- 依次处理 `324.bmp` ~ `371.bmp` 共 48 张图；
- 控制台实时输出每张图的 $(dx, dy)$、圆心与图像中心坐标及处理耗时（平均 ~32ms/帧）；
- 48 张完整诊断标注图统一保存至：
  [`images/求圆中心与图像中心的差值/20260822_0/results/`](file:///d:/Projects/opencvProject/WaferCalibSDK/images/%E6%B1%82%E5%9C%86%E4%B8%AD%E5%BF%83%E4%B8%8E%E5%9B%BE%E5%83%8F%E4%B8%AD%E5%BF%83%E7%9A%84%E5%B7%AE%E5%80%BC/20260822_0/results/)
