# WaferCalibSDK 开发任务清单

## 任务：8视野圆弧求圆心反光平台自适应边缘检测优化
- [x] 1. 根本原因深入分析与实测验证
  - 分析反光背景台架灰度特性与原算法失效机理（`seen_dark` 与起点误判）
  - 编写 Python 端到端验证脚本并在 5 个全量数据集上完成算法验证
- [x] 2. C++ 核心算法实现与重构
  - 修改 `src/c_api/four_view_edge_circle.cpp`：
    - 更新 `extractEdgeDirectional`（引入 `seen_dark` 状态机与局部基准 $b, s$ 自适应亚像素提取）
    - 优化 `detectEdge` 左右背景采样对比与回退机制
    - 优化 `notchAngle` 过滤跨越逻辑与 View 7 Notch 离群抑制
    - 增加两阶段粗圆空间径向滤波（$\pm 1.5\text{ mm}$ 窗口），剥离镜头暗角渐晕与台面反光粗差
- [x] 3. 编译构建与测试验证
  - 使用 VS2022 CMake 重新编译 `Release` 版 `WaferCalibSDK.dll`（0 警告 0 错误）
  - 运行单元测试（`test_four_view_edge_circle.py`、`test_eight_view_edge_circle.py`），全部 PASS
  - 运行全量 5 组真实场景数据集（四组历史数据 + 一组反光平台新数据），验证圆心、半径、Notch 角度与残差指标 100% 达标：
    - `EdgeImage` (全黑 1.5X): Center=(-1.8153, -7.2024), R=149.6705, RMS=34.33 um, Notch=0.0836°
    - `EdgeImage_2.5X_8`: Center=(-1.4518, 84.1928), R=149.7382, RMS=12.37 um, Notch=-0.1693°
    - `EdgeImage_2.5X_thick_8`: Center=(-1.1258, 84.3255), R=149.6596, RMS=11.76 um, Notch=-0.4094°
    - `EdgeImage_1.5X_thick_8`: Center=(-0.7422, 84.2072), R=149.6593, RMS=19.61 um, Notch=-0.4120°
    - `8视野圆弧求圆心\EdgeImage` (反光新样片): Center=(1.6791, -84.0015), R=149.6907, RMS=12.95 um, Notch=1.0641°
- [x] 4. 同步更新文档与生成交付物
  - 同步最新构建的 `WaferCalibSDK.dll` 至 `docs/` 与 `output/CircleCenterOffsetTool/`
  - 更新 `docs/CHANGELOG.md`：记录 2026-11-04 针对反光背景台架的自适应边缘算法升级
  - 更新 `docs/WaferCalibSDK_C_API调用说明.md`：在第 11 节增加反光背景自适应边缘、倒角暗谷定位与初圆几何滤波原理说明
  - 重新导出高保真 PDF `docs/WaferCalibSDK_C_API调用说明.pdf`（29.24 MB）并同步至 `output/pdf/`
  - 更新 `.ai_docs/walkthrough.md` 记录本次优化详细报告
