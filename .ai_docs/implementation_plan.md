# 实施方案：反射率光电响应标定与灰度线性化 LUT 功能实现

## 1. 业务背景与技术原理

在晶圆半导体检测（Wafer Inspection）、膜厚干涉测量与高精度对位中，工业相机拍摄到的表面灰度与被测物质物理反射率密切相关。然而由于 CMOS 传感器存在暗电流偏置、光电转换非线性（PRNU / Gamma 响应）以及光源照度与镜头衰减，导致相机的灰度输出往往偏离理想的物理线性关系，且不同机台之间成像灰度存在系统性漂移。

通过采用 5%、50%、75%、90% 四块已知反射率的漫反射标准板（或标准阶梯靶标），本模块实现：
1. **自动波峰提取**：在中心视场 ROI 内统计 4 个标准反射率的实际灰度波峰分布；
2. **多模式 1D LUT 生成**：
   - **模式 0（分段平台死区阶梯模式）**：精确实现用户图示规则，对目标基准灰度设立死区平台（如 $\pm 5$ 灰度），抑制光学抖动，区间线性拉伸；
   - **模式 1（单调平滑样条曲线模式）**：采用 PCHIP（保形单调三次插值），生成严格平滑单调的无阶梯连续 Tone 曲线；
3. **极速在线查表校正**：生产阶段每帧耗时 $< 2\text{ ms}$（4096×4096 图像），实现反射率与输出灰度的严格线性化。

---

## 2. 数据规律与设计基准

实测数据位于 `D:\Projects\opencvProject\WaferCalibSDK\images\不同反射率_线性矫正`（4 张 4096×4096 Mono8 图像）：
- `5%.bmp`：全图均值 8.14，中心 8.78，波峰在 8；
- `50%.bmp`：全图均值 135.87，中心 148.30，波峰在 145；
- `75%.bmp`：全图均值 195.31，中心 213.06，波峰在 208；
- `90%.bmp`：全图均值 243.21，中心 255.00，波峰在 255（中心过曝）。

标准物理基准设定（理论线性换算）：
- 5% 反射率 $\to$ 目标灰度 13
- 50% 反射率 $\to$ 目标灰度 128
- 75% 反射率 $\to$ 目标灰度 192
- 90% 反射率 $\to$ 目标灰度 230
- 平台死区默认半宽：$W = 5$ 灰度级。

---

## 3. 模块架构与接口设计

### 3.1 C++ 算法模块 (`ReflectanceLutModule`)
- 头文件：`include/wafer_calib/modules/reflectance_lut.hpp`
- 源文件：`src/modules/reflectance_lut.cpp`
- 核心能力：
  1. `ExtractPeaks`：中心 ROI 高斯平滑直方图与亚像素波峰提取；
  2. `GenerateDeadbandLut`：根据图示分段逻辑构建 7 段映射表；
  3. `GenerateSmoothLut`：单调保形平滑三次样条插值；
  4. `ApplyLut`：SIMD 向量化高速图像映射；
  5. `RenderDiagnostic`：高分辨率四合一工业诊断看板（直方图叠加、LUT 曲线、线性度度量、灰阶对比）。

### 3.2 C API 导出层
- 在 `include/wafer_calib/c_api/wafer_calib_c.h` 中新增：
  - 结构体 `WaferReflectanceLutConfig` 与 `WaferReflectanceLutResult`
  - 离线标定接口：`Wafer_CalibrateReflectanceLut` 与 `Wafer_CalibrateReflectanceLutFromFiles`
  - 在线校正接口：`Wafer_ApplyLutToImage`
  - 配方文件持久化接口：`Wafer_SaveLutToFile` 与 `Wafer_LoadLutFromFile`
- 在 `src/c_api/wafer_calib_c.cpp` 中实现。

---

## 4. 验证与交付物
1. 编写专用测试工程 `tests/test_reflectance_lut.cpp`；
2. 构建编译并测试 4 张实测真实图像；
3. 输出高分辨率综合诊断大图至原测试数据目录及文档目录；
4. 更新 `docs/WaferCalibSDK_C_API调用说明.md` 第八单元并导出新版 PDF 与 DLL。