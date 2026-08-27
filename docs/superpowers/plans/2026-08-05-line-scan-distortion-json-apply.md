# 线扫畸变 JSON 导出与离线应用 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 导出一次彩色线扫标定模型为 JSON，并把同一模型直接应用到新 BGR 图，验证序列化一致性与新图的几何泛化表现。

**Architecture:** 在现有 header-only `line_scan_color_distortion` 模块中增加 JSON 读写和从配方重建的校正入口；JSON 保存所有反向 `remap` 参数。扩展现有 sample 为 `--calibrate`/`--apply` 两种显式命令，自动化测试验证同图 JSON 回放逐像素一致和输入保护；新图作为 sample 的实际泛化运行输入。

**Tech Stack:** C++17、OpenCV 4.1 (`core`、`imgproc`、`imgcodecs`)、标准库文件 I/O 与正则解析、CMake。

## Global Constraints

- 不修改 WaferCalibSDK 的公开 C/C++ 接口或 ABI。
- 输入为连续 `CV_8UC3` / BGR8，应用图宽度必须等于 JSON 的 `input_width=11008`；高度允许不同。
- JSON 不记录或校验相机、镜头、倍率、线频、速度、编码器等工艺元数据。
- 只使用 `line_scan_x_quadratic_y_affine_v1` 的 X 二次与 Y 仿射模型；新图应用阶段不检测圆点、不拟合系数。
- 输出继续使用 `BORDER_CONSTANT`，黑色区域表示源图范围外，非图像内容。
- 当前目录不是 Git 仓库；以构建、测试和结果图验证代替提交。

---

### Task 1: JSON 配方序列化和反序列化

**Files:**
- Modify: `samples/line_scan_color_distortion.hpp`
- Modify: `tests/test_line_scan_color_distortion.cpp`

**Interfaces:**

```cpp
bool saveCalibrationJson(const CalibrationResult& calibration,
                         const cv::Size& calibration_image_size,
                         double point_spacing_mm,
                         const std::filesystem::path& json_path);

bool loadCalibrationJson(const std::filesystem::path& json_path,
                         CalibrationResult& calibration,
                         cv::Size& calibration_image_size,
                         double& point_spacing_mm);

bool correctImageByCalibrationFile(const cv::Mat& bgr,
                                   const CalibrationResult& calibration,
                                   const cv::Size& calibration_image_size,
                                   cv::Mat& corrected_bgr);
```

- [ ] **Step 1: Write the failing JSON round-trip test**

Extend `test_line_scan_color_distortion.cpp` after successful in-memory calibration:

```cpp
const std::filesystem::path json = std::filesystem::temp_directory_path() / "line_scan_calibration_test.json";
if (!saveCalibrationJson(calibration, input.size(), 5.0, json)) return 12;
CalibrationResult loaded;
cv::Size saved_size;
double saved_spacing = 0.0;
if (!loadCalibrationJson(json, loaded, saved_size, saved_spacing)) return 13;
cv::Mat corrected_from_json;
if (!correctImageByCalibrationFile(input, loaded, saved_size, corrected_from_json)) return 14;
if (cv::norm(corrected - corrected_from_json, cv::NORM_INF) != 0.0) return 15;
```

Also write a width-mismatch check using a BGR image with `input.cols - 1`; it must return false. Do not test a height mismatch as a failure.

- [ ] **Step 2: Run the test and verify it fails**

Build and run:

```powershell
cmake --build build --config Release --target test_line_scan_color_distortion
& .\build\Release\test_line_scan_color_distortion.exe
```

Expected: compilation fails because the JSON functions are not declared.

- [ ] **Step 3: Implement strict JSON read/write**

Add `#include <filesystem>`, `<fstream>`, `<iomanip>`, `<regex>`, and `<sstream>` to the header. `saveCalibrationJson` writes UTF-8 JSON with `std::setprecision(17)` for all doubles and fields:

```text
schema, input_width, input_height, input_type, point_spacing_mm,
source_x[3], source_y[3], output_origin[2], output_pitch_pixels,
affine_x_rms_pixels, fitted_x_rms_pixels, y_affine_rms_pixels
```

`loadCalibrationJson` accepts only `schema == "line_scan_x_quadratic_y_affine_v1"`, `input_type == "BGR8"`, positive width/height/pitch/spacing, arrays of exact required length, and finite coefficients. It returns false on missing or invalid fields.

`correctImageByCalibrationFile` requires `bgr.type() == CV_8UC3` and `bgr.cols == calibration_image_size.width`, permits any positive height, then calls existing `correctImage`.

- [ ] **Step 4: Run the test and verify it passes**

Run the same target. Expected: the JSON reload produces a byte-identical corrected image; wrong width is rejected; original in-memory geometry assertions remain green.

### Task 2: 扩展 Sample 为明确的标定/应用命令

**Files:**
- Modify: `samples/sample_line_scan_color_distortion.cpp`
- Test: `tests/test_line_scan_color_distortion.cpp`

**Interfaces:**

```text
sample_line_scan_color_distortion.exe --calibrate <calibration_bmp> <calibration_json>
sample_line_scan_color_distortion.exe --apply <calibration_json> <input_bgr_bmp> <output_bmp>
```

- [ ] **Step 1: Write the sample command behavior assertions**

Keep default invocation backward-compatible: it calibrates the repository `1.bmp` and writes existing diagnostic and corrected BMP files. For explicit `--calibrate`, require exactly two paths, create JSON plus sibling `output_line_scan_detection.bmp` and `output_line_scan_corrected.bmp`. For `--apply`, require exactly three paths, load JSON, apply it without invoking `createCalibration`, and write only the requested output BMP.

- [ ] **Step 2: Implement command parsing and output messages**

Use `std::filesystem::u8path` for every argument. Return a distinct nonzero code for invalid syntax, image read failure, calibration failure, JSON write/load failure, compatibility rejection, correction failure, and output write failure.

For `--apply`, print:

```text
已加载畸变配方: <json path>
标定尺寸: <width>x<height>
应用图尺寸: <width>x<height>
校正图: <output path>
```

Do not print an assertion that new-image geometry was verified; only the test/sample analysis after application may state that.

- [ ] **Step 3: Build and run calibration export plus same-image replay**

```powershell
cmake -S . -B build
cmake --build build --config Release --target sample_line_scan_color_distortion test_line_scan_color_distortion
& .\build\Release\sample_line_scan_color_distortion.exe --calibrate `
  images\线扫相机畸变矫正\1.bmp `
  images\线扫相机畸变矫正\line_scan_distortion.json
& .\build\Release\sample_line_scan_color_distortion.exe --apply `
  images\线扫相机畸变矫正\line_scan_distortion.json `
  images\线扫相机畸变矫正\1.bmp `
  images\线扫相机畸变矫正\output_line_scan_replay.bmp
```

Expected: both processes exit `0`; replay BMP is byte-identical to the in-memory correction output.

### Task 3: 在用户提供的新图上做离线泛化验证

**Files:**
- Create: `samples/sample_line_scan_apply_analysis.cpp`
- Modify: `CMakeLists.txt:40-57` (sample glob automatically discovers the new source)

**Interfaces:**

```text
sample_line_scan_apply_analysis.exe <new_input_bmp> <calibration_json> <output_bmp>
```

- [ ] **Step 1: Write the failing new-image analysis test**

Add test helpers that call `detectYellowCenters` before and after applying a loaded JSON calibration. If both images yield at least 60 full centers, calculate affine row slope and first-row horizontal spacing standard deviation; require:

```cpp
std::abs(after_slope) < std::abs(before_slope);
after_spacing_stddev <= before_spacing_stddev;
```

If either image has fewer than 60 centers, do not fail the application test; return the explicit status `geometry_not_verifiable`.

- [ ] **Step 2: Implement analysis sample**

The executable loads JSON and BGR input, applies `correctImageByCalibrationFile`, writes the requested BMP, and reports either the two before/after metrics or `仅完成兼容性应用，未完成点阵几何验证`.

- [ ] **Step 3: Build and run on the supplied new image**

```powershell
& .\build\Release\sample_line_scan_apply_analysis.exe `
  images\线扫相机畸变矫正\Image_20260805110730011.bmp `
  images\线扫相机畸变矫正\line_scan_distortion.json `
  images\线扫相机畸变矫正\output_Image_20260805110730011_corrected.bmp
```

Expected: output has the same BGR type and dimensions as the new input. When the yellow grid is detectable, report whether row slope and horizontal spacing improve; otherwise report compatibility-only result.

- [ ] **Step 4: Visually inspect the new-image output**

Open source and corrected BMP files. Confirm color is preserved, expected source-out-of-range black border is present only at map boundaries, and no unexpected full-frame corruption, channel swap, or resolution change occurs.

- [ ] **Step 5: Record verification instead of committing**

Do not run `git add` or `git commit` because `D:\Projects\opencvProject\WaferCalibSDK` has no `.git` repository. Report exact commands, exit codes, JSON path, output image paths, and measured before/after values.
