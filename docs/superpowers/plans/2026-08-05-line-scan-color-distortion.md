# 彩色线扫点阵畸变校正 Sample Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 `images/线扫相机畸变矫正/1.bmp` 提供可运行的彩色线扫点阵校正 Sample 和集成测试，输出诊断图与 BGR 校正图。

**Architecture:** 新增仅供 sample/test 使用的头文件实现，负责黄色圆点检测、点阵索引、保守的 X 二次加 XY 仿射反向映射及 BGR `cv::remap`。头文件实现避免被当前 `samples/*.cpp` 的自动 glob 当作独立 sample。Sample 只负责查找图片、调用模块、写入两张 BMP；测试在真实图像上验证建模方向和校正效果。SDK C API 与已有库代码不修改。

**Tech Stack:** C++17、OpenCV 4.1 (`imgproc`、`imgcodecs`)、CMake、Windows UTF-8 路径辅助函数。

## Global Constraints

- 输入为 BGR 24 bit 图像；校正输出保持 `CV_8UC3`，不改变 WaferCalibSDK 的公开头文件、实现或 ABI。
- 相邻黄色圆点物理间距固定为 `5.0 mm`。
- 上下边界被裁断的黄色圆点不参与建模；左右横向覆盖的完整圆点可以参与。
- 采用 `source_x = ax0 + ax1*u + ax2*u^2` 与 `source_y = ay0 + ay1*u + ay2*v`，不增加高阶 Y 或通用高阶 XY 项。
- 当前目录不是 Git 仓库；每个任务完成后以构建和测试结果代替提交。

---

### Task 1: 实现可独立测试的彩色线扫校正模块

**Files:**
- Create: `samples/line_scan_color_distortion.hpp`
- Test: `tests/test_line_scan_color_distortion.cpp`

**Interfaces:**
- Consumes: `const cv::Mat& bgr`, `double point_spacing_mm`。
- Produces:

```cpp
namespace line_scan_color_distortion {
struct CalibrationResult {
    std::vector<cv::Point2f> centers;
    std::vector<cv::Point2i> lattice_indices;
    cv::Vec3d source_x_coefficients;
    cv::Vec3d source_y_coefficients;
    double affine_x_rms_pixels = 0.0;
    double fitted_x_rms_pixels = 0.0;
    double y_affine_rms_pixels = 0.0;
    double pixels_per_mm_x = 0.0;
    double pixels_per_mm_y = 0.0;
};

bool createCalibration(const cv::Mat& bgr, double point_spacing_mm,
                       CalibrationResult& result, cv::Mat& diagnostic_bgr);
bool correctImage(const cv::Mat& bgr, const CalibrationResult& calibration,
                  cv::Mat& corrected_bgr);
bool detectYellowCenters(const cv::Mat& bgr, std::vector<cv::Point2f>& centers);
}
```

- [ ] **Step 1: Write the failing integration test**

Create `tests/test_line_scan_color_distortion.cpp` with a wished-for interface. Locate `images/线扫相机畸变矫正/1.bmp` by walking parent directories, read with `wafer_calib::readImageUnicode(..., cv::IMREAD_COLOR)`, and assert:

```cpp
line_scan_color_distortion::CalibrationResult calibration;
cv::Mat diagnostic;
if (!line_scan_color_distortion::createCalibration(input, 5.0, calibration, diagnostic)) return 2;
if (calibration.centers.size() < 60 || calibration.lattice_indices.size() != calibration.centers.size()) return 3;
if (calibration.affine_x_rms_pixels < 1.0) return 4;
if (calibration.fitted_x_rms_pixels >= calibration.affine_x_rms_pixels * 0.35) return 5;
if (calibration.y_affine_rms_pixels > 0.5) return 6;
cv::Mat corrected;
if (!line_scan_color_distortion::correctImage(input, calibration, corrected)) return 7;
if (corrected.type() != CV_8UC3 || corrected.size() != input.size()) return 8;
```

Add local helpers that measure yellow-center row inclination and spacing standard deviation before/after correction. Require the corrected inclination magnitude to be lower and both spacing standard deviations not to increase.

- [ ] **Step 2: Run the test and verify expected failure**

Temporarily add the executable to CMake, configure the existing build directory, and run:

```powershell
cmake --build build --config Release --target test_line_scan_color_distortion
& .\build\Release\test_line_scan_color_distortion.exe
```

Expected: build failure because `samples/line_scan_color_distortion.hpp` and its functions do not exist.

- [ ] **Step 3: Implement yellow-center detection and lattice assignment**

Create the header-only implementation. In `detectYellowCenters`, convert BGR to HSV and use a calibrated range around the image's orange-yellow dots (`H=5..20`, `S>=55`, `V>=45`), then run `connectedComponentsWithStats`. Keep components with area `7500..10000`, width/height ratio `0.65..1.5`, and return their centroid.

In `createCalibration`, group centers by X coordinate using a 120 px clustering tolerance. Sort each column by Y. Retain a row only when its component is complete (detected area remains in the accepted range), and build points from the first three complete rows across at least 20 columns. Store every accepted `(column, row)` pair in `lattice_indices`.

Fit the reference affine matrix from lattice `(column,row)` to source centers. Store its X residual RMS in `affine_x_rms_pixels`. Fit source X with `[1,column,column^2]`; fit source Y with `[1,column,row]`; store their RMS values and the X/Y pixel-per-mm scales using the 5 mm pitch.

Generate `diagnostic_bgr` from the input image, draw accepted centers and `(column,row)`, draw the fitted regular grid, and show residual vectors.

- [ ] **Step 4: Run the test and verify expected pass**

Run the same target and executable. Expected: all numerical assertions pass, the diagnostic is `CV_8UC3`, and the source X quadratic fit is materially better than the baseline affine fit.

- [ ] **Step 5: Implement BGR remap and rerun the test**

In `correctImage`, construct `CV_32FC1` `map_x` and `map_y` at the input image size. Let the output coordinate use the fitted X pitch as the common physical scale; calculate lattice coordinates `(u,v)` relative to the fitted source origin and map them back with the stored coefficient vectors. Apply:

```cpp
cv::remap(bgr, corrected_bgr, map_x, map_y,
          cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar());
```

Run the test executable again. Expected: the output is same-size BGR, the yellow-dot rows are less inclined, and neither horizontal nor vertical yellow-center spacing variability increases.

### Task 2: 增加可运行 Sample 和 CMake 注册

**Files:**
- Create: `samples/sample_line_scan_color_distortion.cpp`
- Modify: `CMakeLists.txt:46-57`
- Test: `tests/test_line_scan_color_distortion.cpp`

**Interfaces:**
- Consumes: `line_scan_color_distortion::createCalibration` and `correctImage` from Task 1.
- Produces: `images/线扫相机畸变矫正/output_line_scan_detection.bmp` and `images/线扫相机畸变矫正/output_line_scan_corrected.bmp`.

- [ ] **Step 1: Extend the existing test target definition**

Register the new test executable; it includes the header-only implementation directly, so it does not add code to the SDK library:

```cmake
add_executable(test_line_scan_color_distortion tests/test_line_scan_color_distortion.cpp)
target_link_libraries(test_line_scan_color_distortion PRIVATE WaferCalibSDK ${OpenCV_LIBS})
```

- [ ] **Step 2: Write the sample behavior check**

Create the Sample with the same parent-directory discovery logic as the test. Read `1.bmp` in color, call the two Task 1 functions, write BMP files with `cv::imencode` plus `std::ofstream` on Windows, and return nonzero on read, calibration, correction, or write failure.

The required observable output is:

```text
有效黄色圆点: <count>
X 仿射 RMS: <pixels> px
X 二次拟合 RMS: <pixels> px
Y 仿射 RMS: <pixels> px
诊断图: <absolute path>
校正图: <absolute path>
```

- [ ] **Step 3: Build and run the sample**

Configure and build the Release target:

```powershell
cmake -S . -B build
cmake --build build --config Release --target sample_line_scan_color_distortion test_line_scan_color_distortion
& .\build\Release\sample_line_scan_color_distortion.exe
& .\build\Release\test_line_scan_color_distortion.exe
```

Expected: both processes exit `0`; both output BMP files exist and are readable by OpenCV.

- [ ] **Step 4: Visually inspect the generated output images**

Open both BMP files. Confirm the diagnostic labels and residual vectors are visible, and confirm the corrected BGR image has horizontalized yellow-dot rows without channel loss, black image corruption, or cropping beyond normal `remap` borders.

- [ ] **Step 5: Record verification instead of committing**

Do not run `git add` or `git commit` because `D:\Projects\opencvProject\WaferCalibSDK` has no `.git` repository. Report the exact build targets, test process exit codes, and generated image paths in the handoff.
