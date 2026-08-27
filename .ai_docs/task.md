# 任务清单：新增圆中心与图像中心差值计算接口

- [x] 1. 新增 C++ 模块头文件 include/wafer_calib/modules/circle_center_offset.hpp
- [x] 2. 新增 C++ 模块源文件 src/modules/circle_center_offset.cpp
- [x] 3. 更新 SDK 主头文件 include/wafer_calib/wafer_calib.hpp
- [x] 4. 在 C API 头文件 include/wafer_calib/c_api/wafer_calib_c.h 中声明 Wafer_FindCircleCenterOffset
- [x] 5. 在 C API 源文件 src/c_api/wafer_calib_c.cpp 中实现 Wafer_FindCircleCenterOffset
- [x] 6. 新增示例程序 samples/sample_circle_center_offset.cpp
- [x] 7. 新增自动化测试程序 	ests/test_circle_center_offset.cpp
- [x] 8. 更新 CMakeLists.txt 添加测试工程
- [x] 9. 编译并运行自动化测试，验证 48 张真实图片 100% 通过
- [x] 10. 更新 C API 调用说明文档 docs/WaferCalibSDK_C_API调用说明.md
- [x] 11. 生成 Walkthrough 并完成交付
