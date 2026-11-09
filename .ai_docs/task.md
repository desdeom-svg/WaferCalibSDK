# 任务清单：水平标定线角度检测算法兼容性与鲁棒性全面重构

- [x] **阶段 1：特征机理诊断与根因剖析**
  - [x] 解析 `C:\Users\HYC\Desktop\CameraAjustCalibration` 目录下 `Mark_1.bmp`, `Mark_2.bmp`, `Mark_3.bmp` 及历史实测图、合成图的视觉特征
  - [x] 查明旧算法失效根因：单一反向 Otsu 假设、缺乏空间方向性滤波、暗角与背景漂移吞没弱特征、连通域筛选条件僵化
  - [x] 确定新算法多模态检测架构（双极性垂直高通滤波 + 水平方向性连通聚合 + 密集亚像素直线拟合 + 物理条带对称中轴与主聚类优选）

- [x] **阶段 2：算法原型设计与交叉全量验证**
  - [x] Python 原型设计并验证垂直一维高通滤波（`boxFilter(1, K)` 消除背景漂移）
  - [x] 验证水平闭运算在弱刻痕/断续线上的长程连接性能
  - [x] 验证多候选线垂直位置聚类与宽条带上下伴随边缘的无偏中轴对称合成
  - [x] 全样本验证：历史合成用例（1/2/3）、实测用例（`角度调整1/2.bmp`）、新样本（`Mark_1/2/3.bmp`）100% 检出且高精度契合

- [x] **阶段 3：C++ SDK 核心实现与工程重构**
  - [x] 重构 `src/modules/line_angle.cpp` 中的 `LineAngleModule::extractLineInfo`
  - [x] 严格遵守 K&R 风格、4 空格缩进与简体中文注释规范
  - [x] 重新编译 `WaferCalibSDK.dll`、`test_line_angle.exe`、`test_two_view_line_angle.exe`

- [x] **阶段 4：回归测试、全量验证与文档归档**
  - [x] 运行 `test_line_angle.exe` 验证单视野单图及贴边图测试 100% PASS
  - [x] 运行 `test_two_view_line_angle.exe` 验证双视野大基线标定测试 100% PASS
  - [x] 编写并执行针对 `CameraAjustCalibration` 目录的 C API 全功能自动化测试（全部 100% 成功，单帧约 108ms）
  - [x] 更新 `docs/CHANGELOG.md` 及 `docs/WaferCalibSDK_C_API调用说明.md`
  - [x] 汇总测试报告与交付说明
