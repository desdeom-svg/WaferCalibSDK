# 5X 畸变标定与 MyImage / MyImage1 矫正效果量化验证方案

## 1. 任务背景与验证目的

用户提供了位于 `C:\Users\HYC\Desktop\畸变验证` 的测试样本，要求：
- 使用其中的 5X 图片（`Mark__5X_0.bmp` ~ `Mark__5X_4.bmp`）作为**标定集**；
- 独立验证未参与标定的测试图片（`MyImage.bmp` 与 `MyImage1.bmp`）在**矫正前 vs 矫正后**的真实畸变改善情况。

## 2. 标定板与数据集几何特征

- **标定板类型**：10 行 × 10 列规则大圆点阵，中心含 1 个用于方向与序号判定的中心对角特征点，共计 101 个圆斑；
- **光学倍率**：5X 显微物镜；
- **点阵步距 (Pitch)**：约 333.3 像素（$X$ 轴平均 $333.41\text{ px}$，$Y$ 轴平均 $333.27\text{ px}$）；
- **标定集**：
  - `Mark__5X_0.bmp`：中心基准视场（Center Mark 位于 $(2048.5, 2043.1)$）；
  - `Mark__5X_1.bmp`：左下视场（Center Mark 偏移约 $(-500, +495)\text{ px}$）；
  - `Mark__5X_2.bmp`：左上视场（Center Mark 偏移约 $(-500, -505)\text{ px}$）；
  - `Mark__5X_3.bmp`：右上视场（Center Mark 偏移约 $(+500, -505)\text{ px}$）；
  - `Mark__5X_4.bmp`：右下视场（Center Mark 偏移约 $(+500, +495)\text{ px}$）；
- **验证集**：
  - `MyImage.bmp`：中心 Mark 位于 $(1826.5, 1822.0)$；
  - `MyImage1.bmp`：中心 Mark 位于 $(2271.0, 1822.8)$。

## 3. 科学评估体系与指标设计

为彻底消除刚体平移、旋转及像元微小非方形比（线性拉伸）对光学畸变评测的干扰，采用机器视觉权威的几何畸变与直线度分解模型：
1. **行/列直线度残差 (Straightness Residuals)**：
   - 对 10 行分别进行直线拟合，求出各行圆心到拟合直线的法向偏差，统计行直线度 RMS 与最大偏差；
   - 对 10 列分别进行直线拟合，求出各列圆心到拟合直线的法向偏差，统计列直线度 RMS 与最大偏差；
   - 统计综合直线度 RMS：$\sqrt{\frac{1}{200}\sum d^2}$；
2. **纯非线性光学畸变残差 (Non-linear Grid Residuals)**：
   - 构建 2D 仿射基准模型消除线性平移、转角与剪切，求解每个点的纯非线性光学畸变位移矢量 $\mathbf{d} = (dx, dy)$；
   - 统计非线性畸变 RMS、最大畸变量 $d_{max}$ 及平均畸变量 $\bar{d}$；
3. **畸变改善率 (Improvement Rate %)**：
   $$\text{Improvement Rate} = \frac{\text{Metric}_{raw} - \text{Metric}_{corrected}}{\text{Metric}_{raw}} \times 100\%$$
