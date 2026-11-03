# WaferCalibSDK 开发任务清单

## 最近完成任务
- [x] 新增支持每组 N 张图像平均后再进行反射率 LUT 拟合的核心函数与 C ABI 接口
  - 在 `include/wafer_calib/modules/reflectance_lut.hpp` 声明 `WaferReflectanceLutCalibrator::CalibrateMultiFrame`
  - 在 `src/modules/reflectance_lut.cpp` 中实现：校验输入尺寸，若 $N > 1$ 利用 `cv::accumulate` 对每组 $N$ 张图像进行 32 位浮点累加均值化，转为 `CV_8UC1` 后复用现有高精度标定流水线；若 $N = 1$ 直接透传
  - 在 `include/wafer_calib/c_api/wafer_calib_c.h` 增加 `Wafer_CalibrateReflectanceLutMultiFrame` 与 `Wafer_CalibrateReflectanceLutFromFilesMultiFrame`
  - 在 `src/c_api/wafer_calib_c.cpp` 中实现，原接口内联委托给多帧版本（$N=1$）实现 100% 向后兼容
  - 导出符号验证：`dumpbin /EXPORTS` 确认导出序号 477 与 478 符号就绪
  - 动态库已同步更新至 Release 构建并拷贝至 C# 端测试套件
