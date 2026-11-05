# 任务清单：晶圆圆心与 Notch 检测算法方法论级全面重构与验证

- [x] **阶段 1：全景视觉诊断与方法论级弊端剖析**
  - [x] 生成 `D:\images\谷神星\标准化\8视野圆弧求圆心` 下全部 9 个子文件夹（6 组 8 视野 + 3 组 4 视野）及历史 `EdgeImage` 的全景缩略拼接图
  - [x] 定位 `EdgeImage_8` 无法识别的三重根因（存在宽约 1.9 mm 的去边环形成内外双圆弧 + 整行最大值抬高阈值漏检外圆弧 + 去边环亮度导致 `sign_x` 镜像误判）
  - [x] 总结旧算法在产线多模态场景（反光底座、内外双圆弧、暗场窄脉冲亮线、IC Die 网格线、Notch 内孤立微尘）下的五大方法论弊端
- [x] **阶段 2：新一代通用算法架构设计与原型验证**
  - [x] 设计“确定性几何扫描方向 + 局部双窗口多候选亚像素提取 + 跨行 2D 连续圆弧链聚类 + 视野切线斜率物理约束 + 多视野全局共圆一致性优选”架构
  - [x] 设计 View 7 物理深度窗引导的 2D 连续链 Notch 轮廓提取算法
  - [x] 在 Python 原型中验证全部 11 组真实数据集（7 组 8 视野 + 4 组 4 视野）及合成数据集 100% 通过
- [x] **阶段 3：C++ SDK 源码移植與 DLL 编译**
  - [x] 重构 `src/c_api/four_view_edge_circle.cpp`（严格遵循 K&R 风格与 4 空格缩进及中文注释规范）
  - [x] 编译 `Release x64` 版 `WaferCalibSDK.dll` 并同步至 `docs/` 与 `output/`
- [x] **阶段 4：全量测试验证与文档/PDF 更新**
  - [x] 运行合成契约单元测试（`test_four_view_edge_circle.py` & `test_eight_view_edge_circle.py`）100% 通过
  - [x] 运行全量 11 组真实产线数据集基准测试（`verify_all_eight_view_datasets.py`）100% 通过
  - [x] 更新 `.ai_docs/` 开发文档、`docs/CHANGELOG.md`、`docs/WaferCalibSDK_C_API调用说明.md` 并重新生成 PDF
