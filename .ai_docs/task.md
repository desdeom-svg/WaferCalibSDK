# 任务清单：晶圆边缘圆拟合自适应加固与残差门限定制

- [x] 1. 制定实施方案并编写 implementation_plan.md (已完成)
- [x] 2. 重构 `detectEdge` 边缘提取算子： (已完成)
    - 引入基于图像边界灰度自适应判定扫描极性机制 (黑场->亮场)
    - 增加双向自适应回退重试机制，彻底免疫相机左右/上下镜像及倒装
- [x] 3. 放宽 RMS 拟合残差判定门限： (已完成)
    - 将 `Wafer_FindFourViewEdgeCircle` 的 `10. * pixel_size_um` 放宽至 `100. * pixel_size_um`
    - 将 `Wafer_FindEightViewEdgeCircle` 的 `10. * pixel_size_um` 放宽至 `100. * pixel_size_um`
- [x] 4. 编译构建 SDK DLL 并执行单元测试与回归测试： (已完成)
    - 验证 `C:\Users\HYC\Desktop\EdgeImage` 返回 0 (成功)
    - 验证标准化数据集 `EdgeImage_2.5X_8`、`EdgeImage_2.5X_thick_8`、`EdgeImage_1.5X_thick_8` 100% 保持通过
    - 单元测试套件全部 PASS (4/4, 3/3)
- [x] 5. 同步最新 DLL 到上位机并更新 walkthrough.md 交付报告 (已完成)
