# 任务清单：双视野大基线水平标定线全局角度检测

- [x] 核心算法与数据结构扩展 (LineAngleModule) <!-- id: 0 -->
    - [x] 在 `include/wafer_calib/modules/line_angle.hpp` 中声明 `findTwoViewHorizontalLineAngle` 及双视野结果结构体 <!-- id: 1 -->
    - [x] 在 `src/modules/line_angle.cpp` 中抽取高精度单图直线亚像素参数提取器（获取斜率、中心切线高度 $v_c$） <!-- id: 2 -->
    - [x] 实现跨两视野大基线全局角度解算数学模型（基于 $\Delta X_{\text{stage}}$ 与 $\Delta v \times s_y$） <!-- id: 3 -->
    - [x] 实现 `3000 × 1800` 工业看板综合对齐诊断大图合成（左微观特写、右微观特写、中轴落差标尺、顶部 HUD、底部参数清单） <!-- id: 4 -->
- [x] C API 极简平铺接口导出与封装 <!-- id: 5 -->
    - [x] 在 `include/wafer_calib/c_api/wafer_calib_c.h` 中导出 `Wafer_FindTwoViewHorizontalLineAngle`（去掉 `stage_delta_y_mm`，支持 BGR 缓冲区或 NULL） <!-- id: 6 -->
    - [x] 在 `src/c_api/wafer_calib_c.cpp` 中实现严密参数校验与 C++ 异常防护 <!-- id: 7 -->
- [x] 自动化测试与实测数据验证 <!-- id: 8 -->
    - [x] 新建 `tests/test_two_view_line_angle.cpp`，加载 `images/根据线输出角度/角度调整1.bmp` 与 `角度调整2.bmp` <!-- id: 9 -->
    - [x] 验证位移大基线解算角度、角分转换、局部单图角度对比与诊断大图输出落盘 <!-- id: 10 -->
    - [x] 使用 MSBuild 执行编译与测试套件回归 <!-- id: 11 -->
- [x] 交付物与技术文档更新 <!-- id: 12 -->
    - [x] 编译 Release x64 DLL/LIB 并拷贝至 `docs/` <!-- id: 13 -->
    - [x] 更新 `docs/WaferCalibSDK_C_API调用说明.md` 新增双视野角度检测章节与 C# P/Invoke 签名代码 <!-- id: 14 -->
    - [x] 执行 `tools/convert_docs_to_pdf.py` 重新生成 `docs/WaferCalibSDK_C_API调用说明.pdf` <!-- id: 15 -->
    - [x] 更新 `.ai_docs/walkthrough.md` 记录最终交付成果 <!-- id: 16 -->
