# 线扫仅 X 方向畸变校正 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让线扫彩色点阵标定只补偿 X 方向二次畸变，永不修改 Y 坐标。

**Architecture:** `createCalibration` 仅保留 X 二次拟合用于映射；`correctImage` 为每个输出像素创建 X 映射并令 Y 映射为其行号。JSON 使用新 schema，禁止读取旧的含 Y 剪切配方。

**Tech Stack:** C++17、OpenCV remap、CMake/CTest。

## Global Constraints

- 仅接受 BGR8 (`CV_8UC3`) 线扫样例输入。
- 不修改 WaferCalibSDK 的公开 C API 或 ABI。
- 标定图宽度仍须与应用图宽度一致；高度可不同。

---

### Task 1: X-only 映射与 JSON 回归测试

**Files:**
- Modify: `tests/test_line_scan_color_distortion.cpp`

**Interfaces:**
- Consumes: `line_scan_color_distortion::correctImage`、`saveCalibrationJson`、`loadCalibrationJson`
- Produces: 对 Y 恒等映射、JSON schema 与旧配方拒绝的可执行验证。

- [ ] **Step 1: 写失败测试**

在标定后断言修正图的黄色圆心行倾角与原图接近；读取 JSON 后断言文本含 `line_scan_x_quadratic_only_v1`，并把 schema 改成旧值后断言加载失败。

- [ ] **Step 2: 运行测试确认失败**

Run: `build\\Release\\test_line_scan_color_distortion.exe`

Expected: 因旧实现会将标定图行倾角显著压低、JSON schema 仍为 `line_scan_x_quadratic_y_affine_v1` 而失败。

### Task 2: 实现 X-only 标定配方与映射

**Files:**
- Modify: `samples/line_scan_color_distortion.hpp`

**Interfaces:**
- Produces: `correctImage` 的 `map_y(y,x)=y`；新 JSON schema `line_scan_x_quadratic_only_v1`。

- [ ] **Step 1: 最小实现**

将 `correctImage` 中 Y 映射替换为当前输出行号；保存 JSON 时移除 `source_y`，加载 JSON 时只读取 X 系数与输出参数并拒绝旧 schema。

- [ ] **Step 2: 运行回归测试**

Run: `build\\Release\\test_line_scan_color_distortion.exe`

Expected: exit 0。

### Task 3: 重新构建并离线验证

**Files:**
- Modify: `images/线扫相机畸变矫正/line_scan_distortion.json`
- Generate: `images/线扫相机畸变矫正/output_Image_20260805110730011_x_only_corrected.bmp`

- [ ] **Step 1: 构建 Release**

Run: `cmake --build build --config Release`

- [ ] **Step 2: 生成 X-only JSON 与应用新图**

Run `sample_line_scan_color_distortion --calibrate` 生成新 JSON，再以 `--apply` 处理 `Image_20260805110730011.bmp`。

- [ ] **Step 3: 运行分析样例**

Run: `sample_line_scan_apply_analysis <new.bmp> <json> <output.bmp>`。

Expected: 横向点距标准差下降，输出行倾角与输入接近，不再因旧 Y 剪切增大。
