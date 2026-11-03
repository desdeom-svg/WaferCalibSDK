# WaferCalibSDK 开发任务清单

## 最近完成任务
- [x] 反射率 LUT 标定精简：去掉文件路径多帧接口，专注内存多帧接口
  - 保留内存多帧接口：`Wafer_CalibrateReflectanceLutMultiFrame` (支持每组 N 张 Mono8 图像内存缓冲区平均)
  - 彻底去除文件路径多帧接口：`Wafer_CalibrateReflectanceLutFromFilesMultiFrame`
  - 恢复单帧文件接口：`Wafer_CalibrateReflectanceLutFromFiles` 为接收 4 个文件路径的标准单帧标定
  - 底层 DLL 编译通过，经 `dumpbin /EXPORTS` 确认导出符号仅包含 `Wafer_CalibrateReflectanceLut`、`Wafer_CalibrateReflectanceLutFromFiles` 和 `Wafer_CalibrateReflectanceLutMultiFrame`
