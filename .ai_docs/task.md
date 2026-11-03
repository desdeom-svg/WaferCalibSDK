# WaferCalibSDK 开发任务清单

## 最近完成任务
- [x] 更新开发说明与 API 调用文档
  - 更新 `docs/CHANGELOG.md`：记录 2026-11-03 版本更新，涵盖“畸变实时校正时间优化至 5.7ms”与“新增多帧反射率 LUT 标定接口 `Wafer_CalibrateReflectanceLutMultiFrame`”
  - 更新 `docs/WaferCalibSDK_C_API调用说明.md`：在第 5 节新增 5.7ms 极速校正架构原理与基准测试数据；在第 8 节新增 `Wafer_CalibrateReflectanceLutMultiFrame` 接口规范、参数列表与抗噪机理
  - 更新 `README.md`：同步更新功能特性与接口速查表
  - 重新编译生成高保真印刷级 `docs/WaferCalibSDK_C_API调用说明.pdf`（文件大小 29.16 MB），并同步至 `output/pdf/`
