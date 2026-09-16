# 任务清单：在 WaferCalibTester 中增加 5 视野畸变矫正测试功能

- [x] 1. 扩展 Native P/Invoke 接口声明 (`NativeMethods.cs` 中添加 `Wafer_CreateMultiViewDotGridDistortionTemplate` 等)
- [x] 2. 在 `MainWindow.xaml` 中新增 `TAB 5: 5 视野多点阵联合畸变标定与校正` UI 界面
- [x] 3. 在 `MainWindow.xaml.cs` 中实现 5 视野联合标定、3×3 诊断看板大图渲染、配方导出/导入与实时校正交互逻辑
- [x] 4. 编译 C# 工程并运行，使用真实 5 视野数据集（1.5X / 2.5X / 5X / 10X / 20X）进行全面验证
- [x] 5. 输出交付与使用说明文档 (`walkthrough.md`)
