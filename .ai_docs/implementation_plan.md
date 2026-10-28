# 平场校正（Flat-Field Correction）极简接口开发计划

## 1. 需求与设计原则

根据用户确认的最新方案，本次开发平场校正接口遵循以下原则：
1. **彻底移除 `WaferFlatFieldConfig` 结构体**：
   - 内部固化最优默认参数：均值融合算法 (`combine_method = 0`)、自动以中心 1/4 区域净均值为基准归一化 (`target_mode = 0`)；
   - 仅保留单个截断上限参数 `float max_gain_clamp`（<=0 时默认取 3.5）。
2. **纯内存数据指针传输**：
   - 输入平场序列：`const unsigned char** flat_buffers, int flat_count`；
   - 输入暗场模板：`const unsigned char* dark_template`（Mono8 指针，可为 NULL）；
   - 输出增益矩阵：`float* out_gain_map`（32 位单通道浮点指针）；
   - 实时校正接口：输入待测图指针 `input_image`、输出图指针 `output_image`、暗场指针 `dark_template`、增益指针 `gain_map`；
   - 支持软件将平场增益保存为单通道 32 位浮点图像（Float32 TIFF / BIN），直接以数据指针进行读入与传输。
3. **极简评估指标**：
   - 保留结构体 `WaferFlatFieldMetrics`，仅包含 `raw_nu`（校正前非均匀度%）、`corrected_nu`（校正后非均匀度%）、`vignetting`（渐晕衰减率%）。
4. **极致性能**：
   - 实时在线平场校正内核采用多线程 OpenMP + SIMD 向量化加速，支持原地校正 (`out == in`) 与无暗场快速分支 (`dark == NULL`)，单帧 4096×4096 耗时稳定在 2.5ms 左右。

---

## 2. 详细接口设计与文件组织

### 2.1 C++ 模块层
- **头文件**：`include/wafer_calib/modules/flat_field.hpp`
- **源文件**：`src/modules/flat_field.cpp`
- **核心方法**：
  - `FlatFieldModule::calibrate(...)`
  - `FlatFieldModule::apply(...)`

### 2.2 C ABI 导出层
- **头文件**：`include/wafer_calib/c_api/wafer_calib_c.h`
  - 定义 `WaferFlatFieldMetrics`
  - 声明 `Wafer_CalibrateFlatField`
  - 声明 `Wafer_ApplyFlatFieldCorrection`
- **源文件**：`src/c_api/wafer_calib_c.cpp`
  - 实现 C ABI 参数校验、异常捕获与调用转接。

---

## 3. 验证与测试步骤

1. 编译构建 `WaferCalibSDK.dll`；
2. 编写独立测试程序对现场数据集 `D:\images\谷神星\标准化\预处理\test\5X（900-5）` 进行实测：
   - 暗场：32 帧合成 `dark_template`；
   - 平场：分别对 1# (90%)、2# (75%)、3# (50%)、4# (10%) 各 32 帧执行标定；
   - 验证模板文件保存为 32 位 Float TIFF 与二进制文件；
   - 验证单帧在线校正延时与质量改善指标；
3. 输出测试报告并同步 DLL 到 `WaferCalibTester`。


---

# 暗场-平场-LUT 一体化极速流水线接口与文档升级计划

## 1. 需求与设计原则

1. **三合一融合预处理接口**：
   - 在线预处理链路包含三步：暗场扣除 $\to$ 平场增益校正 $\to$ LUT 线性化查表。
   - 分步调用会经历 2 次大内存读写；通过融合在单次 OpenMP 循环中执行：
     $$I_{\text{out}}[i] = \text{LUT}\left[ \text{clamp}\left( (I_{\text{in}}[i] - D[i]) \times \text{Gain}[i] + 0.5 \right) \right]$$
     单次访存完成全部操作，彻底消除中间缓冲区与缓存换出开销。
   - 接口定义：`Wafer_ApplyPreprocessPipeline(input, output, w, h, dark, gain, lut)`。
   - 支持原地就地校正 (`output == input`)，支持 `dark` 或 `lut` 为 `NULL` 时的自适应分支。

2. **文档与交付物升级**：
   - **更新说明文档**：创建 `docs/CHANGELOG.md`，按发布日期梳理历史重大功能（八视野缺口、平场校正、流水线一体化）。
   - **接口使用说明**：更新 `docs/WaferCalibSDK_C_API调用说明.md`，新增“第十二章：平场校正与暗场-平场-LUT极速预处理流水线”，详细补充物理原理、离线标定流程图、在线实时流水线时序、各接口参数说明与 C# P/Invoke 范例。
   - **PDF 说明文档导出**：调用 `tools/convert_docs_to_pdf.py` 生成高质量的 `WaferCalibSDK_C_API调用说明.pdf`。
