# 任务清单：5X 畸变标定与 MyImage/MyImage1/MyImage2 独立实测验证

- [x] **阶段 1：数据集特征探查与机理分析**
  - [x] 解析 `C:\Users\HYC\Desktop\畸变验证` 下 7 张 4096×4096 图像的灰度与几何特征
  - [x] 确认点阵类型：10×10 大圆点阵（含 1 个中心对角特征点，共 101 点），白圆高亮（灰度 95~115 或 190~240，背景 81 或 175），5X 下间距 pitch 约 333.3 像素
  - [x] 确认 5 视野空间拓扑：`Mark__5X_0` 为中心视野，1/2/3/4 分别对应左下、左上、右上、右下 4 个角视野（平移步距约 500 px）
  - [x] 确认独立测试集：`MyImage.bmp` 与 `MyImage1.bmp` 为独立机位拍摄的点阵测试图

- [x] **阶段 2：全自动高精度畸变标定**
  - [x] 执行单视野畸变标定（基于 `Mark__5X_0.bmp`）：成功提取 100 点，标定拟合 RMS = 0.0913 像素
  - [x] 执行 5 视野多视场联合标定（基于 `Mark__5X_0` ~ `Mark__5X_4`）：成功提取 500 点，全局拟合 RMS = 0.1792 像素

- [x] **阶段 3：独立测试集校正与亚像素高精几何评估**
  - [x] 调用 `Wafer_CorrectImageByDotGridTemplate` 对 `MyImage.bmp` 和 `MyImage1.bmp` 进行单视野与 5 视野校正
  - [x] 提取矫正前 vs 矫正后各点的高精度亚像素灰度质心坐标（10×10 网格拓扑匹配）
  - [x] 计算行直线度、列直线度、综合直线度、2D 仿射网格非线性残差等核心指标
  - [x] 计算畸变改善率（Improvement Rate %）

- [x] **阶段 4：图表可视化与报告输出**
  - [x] 生成 Quiver 畸变位移矢量场放大 300x 对比图（`distortion_comparison_MyImage.png`, `distortion_comparison_MyImage1.png`）
  - [x] 导出全分辨率 4096×4096 校正图像至 `C:\Users\HYC\Desktop\畸变验证`

- [x] **阶段 5：旋转调平（Rotation Leveling）与正交对齐**
  - [x] 解析去畸变后图像的点阵主轴旋转角（实测倾角为 -0.1477° ~ -0.1480°，4096px 跨度落差约 10.56 px）
  - [x] 执行高精度仿射双三次插值旋转调平算法，彻底消除相机/标定板安装偏角
  - [x] 验证调平后图像：网格行水平残差仅 0.0011°（落差降至 0.078 px，实现完美水平正交）
  - [x] 导出全分辨率 4096×4096 BMP 调平图像至 `C:\Users\HYC\Desktop\畸变验证`（`MyImage_leveled.bmp`, `MyImage1_leveled.bmp`, `Mark__5X_0_leveled.bmp` 及单视场版本）
  - [x] 生成旋转调平对比图：`.ai_docs/distortion_leveling_comparison.png`

- [x] **阶段 6：真实晶圆划片槽边缘直线度畸变验证（MyImage2.bmp）**
  - [x] 探查 `MyImage2.bmp` 划片槽结构：精确锁定外侧线（顶部 Y~62、左侧 X~64）与内侧线（顶部 Y~140、左侧 X~142）
  - [x] 调用 5X 畸变标定模型对 `MyImage2.bmp` 完成全分辨率 4096×4096 矫正，并输出至桌面：`MyImage2_corrected_multi.bmp`、`MyImage2_corrected_single.bmp`
  - [x] 沿 3700 像素大跨度进行高密度亚像素边缘提取与 PCA 正交回归拟合
  - [x] 严谨量化各项直线度指标：顶部水平线光学二次弯曲弓高从 0.621 px 降至 0.170~0.415 px（改善率达 33.2% ~ 72.6%），直线度 RMS 降至 0.477~0.510 px
  - [x] 生成高分辨率残差分布曲线与二次弯曲趋势图：`.ai_docs/MyImage2_straightness_comparison.png`
