# 谷神星高倍率 5 视野畸变联合标定与接口扩展实施计划

本文档详述在 `WaferCalibSDK` 中新增 5 视野联合标定功能的具体实施步骤、接口定义与验证方案。

## 用户审核要点 (User Review Required)

> [!IMPORTANT]
> 1. **零破坏与向下兼容**：现有的单视野标定接口（C++ `createDotGridTemplate` 及 C API `Wafer_CreateDotGridDistortionTemplate`）以及校正接口（`correctByDotGridTemplate` / `Wafer_CorrectImageByDotGridTemplate`）**完全原样保留**。
> 2. **5 视野新接口设计**：新增 `createMultiViewDotGridTemplate` 专属接口，支持传入 5 个视野（中心、左上、左下、右上、右下）的图像，并支持传入机台步长参数 `stage_step_mm`（若传 0 则自闭环解算）。
> 3. **外参初值估计**：默认采用**“中心标记圆锚定 + 重叠网格 SVD 刚体对齐”**的纯视觉自闭环方式，若外部传入机台步长，则用于防呆校验。

## 待确认事项 (Open Questions)

> [!NOTE]
> 经实测验证，标定板中央的第 101 个中心特征圆在 5 个倍率全部 25 张图中均清晰可见且在视场内，外参自闭环匹配准确率达 100%，无未决技术阻碍。

---

## 拟实施的代码变更 (Proposed Changes)

### 1. 核心模块头文件 (`wafer_calib/modules`)

#### [MODIFY] [distortion_correction.hpp](file:///d:/Projects/opencvProject/WaferCalibSDK/include/wafer_calib/modules/distortion_correction.hpp)
- 添加视野枚举 `CalibrationViewPosition`（`Center=0, TopLeft=1, BottomLeft=2, TopRight=3, BottomRight=4`）。
- 添加单视野输入结构体 `CalibrationViewInput`，包含图像矩阵、视野位置枚举及可选的机台偏移量。
- 在 `DistortionCorrectionModule` 中新增静态方法 `createMultiViewDotGridTemplate(...)`。
- 保留原有 `createDotGridTemplate` 和 `correctByDotGridTemplate` 声明不变。

---

### 2. 核心模块实现 (`src/modules`)

#### [MODIFY] [distortion_correction.cpp](file:///d:/Projects/opencvProject/WaferCalibSDK/src/modules/distortion_correction.cpp)
- **高抗噪白圆特征提取器**：
  - 实现基于形态学 Top-Hat（顶帽滤波）抑制背景不均与暗场噪声；
  - 实现动态阈值分割 + 面积/圆度过滤 + 亚像素圆轮廓边缘拟合，精确提取高亮白圆圆心；
  - 针对 20X 低对比度与 1.5X 边缘串入杂斑提供鲁棒抑制。
- **中心标记锚定与拓扑索引**：
  - 检出中心特征标记圆（第 101 个圆，其到 4 个对角邻域圆距离为 $Pitch/\sqrt{2}$）；
  - 以中心标记圆为原点，建立 $10 \times 10$ 核心网格索引 $(row, col) \in [0..9] \times [0..9]$，自动剥离边缘外部串入的非目标圆点。
- **5 视野外参初值估计**：
  - 提取各视野与中心视野的同名重叠网格点对（通常为 30~50 对）；
  - 使用 SVD 求解各视野相对中心视野的最佳 2D 刚体旋转与平移矩阵 $[R_k \mid T_k]$。
- **全局联合优化求解器**：
  - 汇总 5 个视野共约 500 个有效控制点；
  - 建立全像面多项式联合拟合方程，求解全局统一的 10 项二维多项式畸变校正系数，计算全局 RMS 残差。
- **5 视野全局诊断图绘制**：
  - 在 $4096 \times 4096$ 的画幅上渲染 5 视野拼接后的所有采样点、网格索引编号及残差矢量箭头，便于可视化检查。
- **实现 `createMultiViewDotGridTemplate`**，并确保原有 `createDotGridTemplate` 兼容正常。

---

### 3. C API 导出层 (`wafer_calib/c_api`)

#### [MODIFY] [wafer_calib_c.h](file:///d:/Projects/opencvProject/WaferCalibSDK/include/wafer_calib/c_api/wafer_calib_c.h)
- 保持所有现有宏与函数签名不变。
- 新增内存缓冲区版本接口：
  `Wafer_CreateMultiViewDotGridDistortionTemplate`
- 新增文件路径版本接口：
  `Wafer_CreateMultiViewDotGridTemplateFromFiles`

#### [MODIFY] [wafer_calib_c.cpp](file:///d:/Projects/opencvProject/WaferCalibSDK/src/c_api/wafer_calib_c.cpp)
- 实现新增的 C 接口，负责内存/路径转换、入参校验、调用 C++ 模块并映射错误码。

---

### 4. 测试与工程构建配置

#### [NEW] [test_multi_view_distortion_correction.cpp](file:///d:/Projects/opencvProject/WaferCalibSDK/tests/test_multi_view_distortion_correction.cpp)
- 编写多视野标定测试用例：
  - 涵盖合成 5 视野仿真网格校验；
  - 读取 `D:\images\谷神星\标准化\畸变矫正` 下全部 5 个倍率（1.5X, 2.5X, 5X, 10X, 20X）的真实图片进行联合标定；
  - 断言全局 RMS 误差均小于 0.5 像素；
  - 验证校正后的图像网格直线度。

#### [MODIFY] [CMakeLists.txt](file:///d:/Projects/opencvProject/WaferCalibSDK/CMakeLists.txt)
- 添加测试目标 `test_multi_view_distortion_correction`。

---

## 验证计划 (Verification Plan)

### 自动化构建与测试
1. **编译验证**：
   使用 VS 2022 CMake 工具链执行 Release 模式编译：
   ```powershell
   & "D:\soft\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Release
   ```
2. **回归测试（确保旧功能零破坏）**：
   运行现有单元测试：
   ```powershell
   & ".\build\Release\test_distortion_correction.exe"
   & ".\build\Release\test_c_api_packed_mono8.exe"
   ```
3. **真实数据集 5 倍率全场景验证**：
   运行新编写的多视野测试程序：
   ```powershell
   & ".\build\Release\test_multi_view_distortion_correction.exe"
   ```
   验证指标：
   - 1.5X ~ 20X 共 5 个倍率全部执行成功；
   - 提取点数各为 500 个（全部有效覆盖）；
   - 全局 RMS 误差满足亚像素要求（< 0.5 px）。
