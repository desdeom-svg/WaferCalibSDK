# 在 WaferCalibTester 中新增 5 视野畸变标定与校正测试功能规划方案

## 1. 背景与目标
当前 `WaferCalibSDK` 底层已完整支持 **5 视野多点阵联合优化畸变标定**（`Wafer_CreateMultiViewDotGridDistortionTemplate` / `Wafer_CreateMultiViewDotGridTemplateFromFiles`），并生成 3×3 空间拓扑无损拼接的 BGR 综合诊断大图与单一全局多项式畸变配方。
本任务旨在 C# WPF 测试上位机工程 [`D:\Projects\csharpProject\WaferCalibTester`](file:///D:/Projects/csharpProject/WaferCalibTester/) 中增加 **5 视野点阵畸变标定与实时校正** 的完整可视化测试界面与交互逻辑，与现有 4 个模块（十字 Mark 定位、水平线测角、单视野畸变、暗场去噪）保持统一的高品质 UI 风格。

---

## 2. 用户审查与关键设计决策 (User Review Required)

> [!IMPORTANT]
> **5 视野输入拓扑顺序与内存规范**：
> - 5 视野图像数组顺序严格固定为：**`[0: 中心 (Center), 1: 左上 (Top-Left), 2: 左下 (Bottom-Left), 3: 右上 (Top-Right), 4: 右下 (Bottom-Right)]`**；
> - 5 视野联合标定输出的诊断大图为 **3×3 空间拓扑拼接大图**（输出分辨率为 $(W \times 3) \times (H \times 3)$，例如 $4096 \times 4096$ 原图对应 $12288 \times 12288$），SDK C ABI 要求调用方预先分配 $(W \times 3) \times (H \times 3) \times 3$ 字节的 BGR 缓冲区。

> [!TIP]
> **测试样本快捷载入支持**：
> - 界面将提供 **“载入预设样本 (1.5X / 2.5X / 5X / 10X / 20X)”** 下拉选择与一键导入功能，自动载入 `D:\images\谷神星\标准化\标定片\` 下的对应 5 张视野图；
> - 支持用户自定义浏览选择本地 5 张图像（支持单独指定 5 个槽位，或按文件夹一次性载入）。

---

## 3. 拟修改与新增的文件清单 (Proposed Changes)

### C# 原生互操作层 (Native Interop)

#### [MODIFY] [NativeMethods.cs](file:///D:/Projects/csharpProject/WaferCalibTester/Native/NativeMethods.cs)
- 新增 `Wafer_CreateMultiViewDotGridDistortionTemplate`（内存缓冲区模式 P/Invoke 声明）；
- 新增 `Wafer_CreateMultiViewDotGridTemplateFromFiles`（文件路径模式 P/Invoke 声明）；
- 新增 `Wafer_SaveDistortionTemplateToFile` / `Wafer_LoadDistortionTemplateFromFile`。

---

### WPF 视图与交互层 (UI & ViewModel)

#### [MODIFY] [MainWindow.xaml](file:///D:/Projects/csharpProject/WaferCalibTester/MainWindow.xaml)
- 在主界面的 `TabControl` 中新增第 5 个 TabItem：`5. 5视野多点阵联合畸变标定与校正`；
- **左侧控制面板**：
  - **倍率与点阵参数**：点阵网格列数 (Cols，默认 10)、行数 (Rows，默认 10)、圆物理间距 (Spacing mm，如 1.0/0.6/0.3)、位移步长 (Step mm，默认 0.0 自适应)；
  - **预设样本快速选择**：下拉框支持 `1.5X`、`2.5X`、`5X`、`10X`、`20X` 一键加载；
  - **5 视野文件列表槽位**：展示当前加载的 5 个视野路径与状态；
  - **标定建模按钮**：“运行 5 视野联合标定”；
  - **标定指标卡片**：显示总有效圆点数、全画幅 RMS 残差 (px)、标定耗时；
  - **配方管理**：“导出模板 (.json)” 与 “导入已有模板 (.json)”；
  - **校正应用区**：“加载待校正图”、“运行实时畸变校正”、“保存校正后图像”。
- **右侧双视窗图像区域**：
  - 上方窗口（`viewerMultiDiag`）：展示 **3×3 空间拓扑无损拼接 BGR 综合诊断大图**；同时提供下拉切换查看单个视野的原始图；
  - 下方窗口（`viewerMultiCorrected`）：展示 **待校正原图 / 实时校正后的无畸变图像**。

#### [MODIFY] [MainWindow.xaml.cs](file:///D:/Projects/csharpProject/WaferCalibTester/MainWindow.xaml.cs)
- 维护 5 视野图像缓冲数据（`m_multiViewPaths[5]`、`m_multiViewBuffers[5]` 等）；
- 实现 5 视野联合标定调用、3×3 BGR 诊断图解析渲染、模板导出与载入；
- 实现对任意单帧 Mono8 图像的高速畸变校正并送入 `ZoomableImageViewer` 实时缩放渲染。

---

## 4. 验证与测试计划 (Verification Plan)

### 自动化与手动测试
1. **编译测试**：使用 `dotnet build` 编译 `WaferCalibTester.csproj`，确保 0 警告 0 错误；
2. **预设 5 视野样本测试**：
   - 在 UI 中选择 `1.5XImages` 预设样本，点击“运行 5 视野联合标定”；
   - 验证是否成功解算出 RMS 残差 $< 0.15\text{ px}$；
   - 验证上方视窗是否正确显示 3×3 拼接诊断看板大图；
3. **配方导出与导入验证**：
   - 导出为 `.json` 配方，再重新导入，验证多项式系数与元数据完整性；
4. **实时图像校正测试**：
   - 载入 `中心.bmp`，点击“运行畸变校正”，验证校正耗时（$< 5\text{ ms}$）并在下方视窗清晰展示校正前后的对比效果；
   - 点击“保存校正后图像”，验证导出的图像文件完整。
