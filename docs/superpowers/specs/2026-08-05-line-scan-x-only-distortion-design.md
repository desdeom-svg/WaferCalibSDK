# 线扫彩色图像仅 X 方向畸变校正设计

## 目标

将彩色线扫点阵样例的应用映射改为仅 X 方向二次畸变校正，Y 坐标逐像素保持不变。

## 设计

- 标定仍从黄色点阵拟合 `source_x = a0 + a1*u + a2*u^2`，用于改善横向点距不均。
- 应用映射固定为 `source_y = output_y`；不保存、不加载、不使用 Y 仿射缩放或 X 到 Y 的倾斜剪切项。
- JSON schema 更新为 `line_scan_x_quadratic_only_v1`，仅保存 X 系数、图像宽高、点间距、输出原点、输出节距以及 X 质量指标。旧 `line_scan_x_quadratic_y_affine_v1` 文件拒绝加载，避免意外继续做倾斜校正。
- 保留 `CalibrationResult` 中既有 Y 质量字段仅用于标定诊断；其不参与输出映射或 JSON。

## 验收

- 用任意 BGR8 输入及有效标定调用 `correctImage` 时，所有 remap 的 Y 源坐标等于输出行号。
- 标定图的横向点距标准差下降；行倾角不再被校正。
- JSON 往返输出与内存标定输出逐像素一致；旧 schema 被拒绝。
