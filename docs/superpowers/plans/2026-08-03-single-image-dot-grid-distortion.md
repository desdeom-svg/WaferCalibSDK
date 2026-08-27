# Single Image Dot Grid Distortion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and apply a reusable fixed-view Mono8 geometric correction template from one symmetric circular dot-grid image.

**Architecture:** `DistortionCorrectionModule` detects dark dots, groups their X/Y projections into the requested fixed-view grid while rejecting off-grid contamination and allowing limited missing dots, then defines an affine ideal grid and stores a normalized third-order output-to-input polynomial map in `DotGridDistortionTemplate`. The C API converts the ABI-stable template structure to and from the C++ model; application evaluates the map and calls `cv::remap`.

**Tech Stack:** C++17, OpenCV 4.1 (`SimpleBlobDetector`, `findCirclesGrid`, `remap`), existing C DLL ABI and CMake targets.

## Global Constraints

- Accept and output only packed `CV_8UC1` buffers; `step == width`.
- Require the image size to equal the template size when applying a template.
- Keep all current exports unchanged; new C ABI has an explicit fixed-size template struct.
- Grid dimensions and point spacing are caller inputs; sample values are 10 columns, 10 rows, 0.15 mm.
- Use the source coordinate polynomial term order `1, x, y, x2, xy, y2, x3, x2y, xy2, y3` with normalized output pixel coordinates in `[-1, 1]`.

---

### Task 1: Define template contracts and write the failing integration test

**Files:**
- Create: `include/wafer_calib/modules/distortion_correction.hpp`
- Modify: `include/wafer_calib/c_api/wafer_calib_c.h`
- Modify: `include/wafer_calib/wafer_calib.hpp`
- Create: `tests/test_distortion_correction.cpp`

**Interfaces:**
- Produces C++ `DotGridDistortionTemplate`, `DistortionCorrectionModule::createDotGridTemplate`, and `DistortionCorrectionModule::correctByDotGridTemplate`.
- Produces C ABI `WaferDotGridDistortionTemplate`, `Wafer_CreateDotGridDistortionTemplate`, and `Wafer_CorrectImageByDotGridTemplate`.

- [ ] **Step 1: Write the failing test**

Create a 400 × 400 Mono8 image containing a 10 × 10 grid of dark circles whose positions are radially warped around `(200, 200)`. Build a template with `10, 10, 0.15`, correct the image, redetect the corrected circles, and assert the standard deviation of horizontal and vertical neighbor distances is lower after correction. Call both C++ and C APIs, assert the C result buffer contains non-background Mono8 pixels, and assert the diagnostic result is `CV_8UC3` with a color overlay.

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
& '.\\build\\Release\\test_distortion_correction.exe'; exit $LASTEXITCODE
```

Expected: build failure because the distortion module and C API symbols do not exist.

- [ ] **Step 3: Add declaration-only contracts**

Define the C++ template with image width/height, detected-point count, `std::array<double, 10>` X/Y coefficients, and `double rms_error_pixels`. Define the C struct with the same fields and `double output_to_input_x[10]`, `double output_to_input_y[10]`. Add the two function declarations to both public headers.

- [ ] **Step 4: Rebuild and run the test**

Run the Task 1 command again. Expected: linker failure because implementation is not present; this confirms the test reaches the desired public contracts.

### Task 2: Implement template building and diagnostic visualization

**Files:**
- Create: `src/modules/distortion_correction.cpp`
- Modify: `tests/test_distortion_correction.cpp`

**Interfaces:**
- Consumes `const cv::Mat& mono8`, grid dimensions, and point spacing.
- Produces `Status createDotGridTemplate(..., DotGridDistortionTemplate&, cv::Mat&)`.

- [ ] **Step 1: Implement circle-grid detection**

Configure a dark `cv::SimpleBlobDetector` with area and circularity filtering derived from image size. Cluster candidate X/Y projections, retain the most populated requested row/column bands, map each candidate to its nearest grid cell, reject off-grid candidates, and accept only if at least 90% of grid cells contain a point. Return `FeatureNotFound` otherwise.

- [ ] **Step 2: Implement ideal-grid and polynomial fitting**

Use object points `(column * point_spacing_mm, row * point_spacing_mm)`. Solve two 3-term least-squares systems for affine predicted target points. Build a `N × 10` polynomial matrix from normalized target pixel positions; solve independent SVD systems for source X and source Y. Store image dimensions, detected-point count, coefficients, and RMS.

- [ ] **Step 3: Render diagnostics**

Convert the Mono8 calibration image to BGR. Draw source centers and indices in green, target positions in yellow, and a red arrow from each target position to its source center. Render `RMS=... px` and `valid ROI=...` in red text.

- [ ] **Step 4: Run the test to verify it passes its C++ builder assertions**

Run the Task 1 command. Expected: test reaches the C API linker failure only; the C++ builder creates a template and BGR diagnostic.

### Task 3: Implement map evaluation, C ABI bridge, and C API behavior

**Files:**
- Modify: `src/modules/distortion_correction.cpp`
- Modify: `src/c_api/wafer_calib_c.cpp`
- Modify: `tests/test_distortion_correction.cpp`

**Interfaces:**
- Consumes a valid `DotGridDistortionTemplate` or `WaferDotGridDistortionTemplate` and packed Mono8 input.
- Produces a packed same-size Mono8 corrected image and C error code.

- [ ] **Step 1: Implement correction**

For every destination pixel `(x, y)`, normalize it with the stored template dimensions, evaluate the 10 terms for both coordinate polynomials, populate `CV_32FC1` `map_x/map_y`, then call `cv::remap(mono8, corrected_mono8, map_x, map_y, INTER_LINEAR, BORDER_CONSTANT, Scalar(0))`. Reject mismatched image dimensions and non-finite coefficients.

- [ ] **Step 2: Bridge the ABI**

Add private conversion helpers between the C and C++ templates. The C creation call wraps the packed buffer, invokes the builder, copies metadata/coefficients to `out_template`, and copies the BGR diagnostic. The correction call wraps input/output packed buffers, invokes correction, and copies output bytes.

- [ ] **Step 3: Run the full test**

Run:

```powershell
& '.\\build\\Release\\test_distortion_correction.exe'; exit $LASTEXITCODE
```

Expected: exit code 0; C++ and C API paths both reduce grid-spacing variation.

### Task 4: Add the real-image sample and validate all SDK behavior

**Files:**
- Create: `samples/sample_distortion_correction.cpp`
- Modify: `CMakeLists.txt` only if the source glob does not discover the new sample after reconfigure.

**Interfaces:**
- Uses the two new C API functions with `grid_columns=10`, `grid_rows=10`, and `point_spacing_mm=0.15`.
- Produces `images/畸变矫正/output_distortion_detection.bmp` and `images/畸变矫正/output_distortion_corrected.bmp`.

- [ ] **Step 1: Implement the sample**

Find `images/畸变矫正` by walking parent directories, read `点阵.bmp` via `readImageUnicode(..., IMREAD_GRAYSCALE)`, allocate the template and output buffers, create the template, apply it to the same image, and write both outputs using the existing Unicode-safe BMP encoder pattern. Print RMS and valid ROI.

- [ ] **Step 2: Reconfigure and build all targets**

Run:

```powershell
& 'D:\\soft\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe' -S . -B build
& 'D:\\soft\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe' --build build --config Release --target test_distortion_correction sample_distortion_correction test_line_angle test_mark_center test_c_api_packed_mono8 -j 2
```

Expected: successful build of the DLL, new test, new sample, and prior regression targets.

- [ ] **Step 3: Run real-image and regression verification**

Run the new sample and all three existing tests. Inspect the detection output: all 100 dots must be indexed, ideal grid/arrow overlay must be visible, and the corrected output must be Mono8 with the dot grid visibly straightened.
