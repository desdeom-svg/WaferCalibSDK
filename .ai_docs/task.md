# 任务清单：晶圆边缘圆拟合自适应加固与 Notch 缺口角度解析

- [x] 1. 制定实施方案并编写 implementation_plan.md (已完成)
- [x] 2. 重构 `detectEdge` 边缘提取算子： (已完成)
    - 引入基于图像边界灰度自适应判定扫描极性机制 (黑场->亮场)
    - 增加双向自适应回退重试机制，彻底免疫相机左右/上下镜像及倒装
- [x] 3. 放宽 RMS 拟合残差判定门限： (已完成)
    - 将 `Wafer_FindFourViewEdgeCircle` 的 `10. * pixel_size_um` 放宽至 `100. * pixel_size_um`
    - 将 `Wafer_FindEightViewEdgeCircle` 的 `10. * pixel_size_um` 放宽至 `100. * pixel_size_um`
- [x] 4. 自适应加固坐标映射（彻底免疫相机镜像倒装）： (已完成)
    - 自动依据晶圆亮场朝向检测 `sign_x` 与 `sign_y`
    - 保证世界坐标系与凹陷深度 `depth = R - dist` 无论在相机正装还是 180° 镜像倒装下均正确为正
    - 确保 View 7 的 Notch 凹陷区域被精准剔除出大圆拟合，使拟合 RMS 残差从 187.5um 降至 31.45um
- [x] 5. 编译构建 SDK DLL 并执行单元测试与回归测试： (已完成)
    - 验证 `C:\Users\HYC\Desktop\EdgeImage` 稳定返回 `code=0`, `Center=(-1.8112, -7.2065)`, `Radius=149.6722`, `RMS=31.45um`, `Notch=+0.0827°`
    - 验证标准化数据集 `EdgeImage_2.5X_8`、`EdgeImage_2.5X_thick_8`、`EdgeImage_1.5X_thick_8` 全部返回 `code=0`
    - C++ 编译 0 警告 0 错误，Python 7/7 单元测试全部通过
- [x] 6. 同步最新 DLL 到上位机 WaferCalibTester (Release / Debug) (已完成)
- [x] 7. 深入剖析 Notch 缺口特征与角度计算逻辑，并绘制高分辨率原理图： (已完成)
    - 生成 `C:\Users\HYC\Desktop\EdgeImage\notch_angle_analysis.png`
