# 8视野圆弧求圆心反光平台自适应边缘检测优化方案

## 1. 问题背景与失效原因分析

### 1.1 图像现象
针对数据集 `D:\images\谷神星\标准化\8视野圆弧求圆心\EdgeImage`：
- **历史黑场数据集**：晶圆为亮区（~190 DN），承载台为纯黑背景（~15 DN），两者灰度差达 170 DN 以上，呈典型单调阶跃边缘。
- **本次失败数据集**：承载台由于机械阳极氧化或金属反光呈现灰色（~65~75 DN），晶圆内部也是灰色（~80~85 DN），晶圆内部与平台灰度仅差 10~15 DN。
- **物理特征**：无论背景全黑还是反光，晶圆倒角（Bevel）处的光学斜面反射/散射特性必然在图像中形成一条深达 0~15 DN 的**“倒角暗谷”（Trough / Dark Valley）**。

### 1.2 原算法失效机理
原 `extractEdgeDirectional` 函数中：
```cpp
const double bg = quantile(row, 0.10);
const double signal = quantile(row, 0.85);
const double coarseThreshold = bg + 0.40 * (signal - bg);
```
在反光平台下，$bg \approx 10$（倒角暗谷），$signal \approx 82$，计算得 $coarseThreshold \approx 39$（或更高）。
扫描从起始点 $x = 0$ 开始，起始点位于反光平台上，其灰度为 70~75 DN，从一开始就大于 $coarseThreshold$。
算法检测到连续 15 个像素大于阈值，立刻在 $x = 0$ 处判定为晶圆上升沿（`coarse = 0`）。随后因为触发了边界保护条件 `coarse < 130`，整行被判定为异常并直接丢弃。
这导致所有 8 个视野提取到的有效边缘点数不足 30，最终返回错误码 `WAFER_ERR_FEATURE_NOT_FOUND (4)`。

---

## 2. 核心算法优化设计

### 2.1 双模态自适应边缘定位机制（状态机 seen_dark + 倒角暗谷爬升）
- **动态极值与范围判定**：
  在有效范围内检测行像素动态范围 $\Delta = \max - \min$。若 $\Delta < 15.0$ 则跳过。
- **暗谷状态机**：
  设定暗区阈值 $dark\_th = \min + 0.25 \times \Delta$，上升沿粗阈值 $coarse\_th = \min + 0.40 \times \Delta$。
  沿扫描方向行进时，必须先经历暗谷（$row[x] \le dark\_th$，标记 $seen\_dark = true$）。
  只有经历过暗区后，再爬升超越 $coarse\_th$ 并连续 15 个像素保持，才认定为真正的晶圆边缘起点 $coarse$。
  - 黑场背景：起点本身即满足 $\le dark\_th$，$seen\_dark$ 立即置真，进入晶圆后爬升，逻辑与原算法完全契合；
  - 反光平台：起点处于平台反光区（$> dark\_th$），$seen\_dark = false$，不会在起点误触发；光线进入晶圆倒角暗谷时触发 $seen\_dark = true$，随后从暗谷爬升到晶圆内侧，准确定位上升沿。

### 2.2 局部自适应基准与亚像素插值
- 外侧暗场基准 $b$：取 $[coarse - 60, coarse - 10)$ 的 30% 分位数；
- 内侧晶圆基准 $s$：取 $[coarse + 30, coarse + 100)$ 的 70% 分位数；
- 亚像素插值阈值：$th = b + 0.15 \times (s - b)$；
- 搜索区间 $[coarse - 50, coarse + 30)$ 内首次穿越 $th$ 的位置进行线性亚像素插值。

### 2.3 Notch 凹槽检测与杂光抑制优化
- 在 `notchAngle` 计算等深线切面交点时，若两点横向距离跳变过大（伪跳变），直接跳过（`continue`），不向 `crossings` 压入 NaN，确保切面交点数严格为 2，稳定提取 Notch 角度；
- 在 View 7 中对提取的原始点按与粗圆半径偏差（$|\Delta r| \le 2.0\text{ mm}$）进行有效性过滤，剥离机械台底边缘杂光反光。

---

## 3. 验证方案与成功标准

1. **编译验证**：CMake 构建成功，0 警告 0 错误；
2. **单元测试回归**：`test_four_view_edge_circle.py` 与 `test_eight_view_edge_circle.py` 均 PASS；
3. **5 个全量数据集实测比对**：
   - 目标数据集 `8视野圆弧求圆心\EdgeImage` 正常解算，输出中心约为 $(1.68, -84.00)\text{ mm}$，半径约为 $149.69\text{ mm}$，Notch 角度约为 $1.06^\circ$，RMS 残差 $< 20\ \mu\text{m}$；
   - 历史 4 个数据集指标完全一致，无任何回归。
4. **交付物齐备**：
   - 更新文档：`CHANGELOG.md`、`WaferCalibSDK_C_API调用说明.md`、`.ai_docs/walkthrough.md`；
   - 重新生成高保真 PDF `docs/WaferCalibSDK_C_API调用说明.pdf`。
