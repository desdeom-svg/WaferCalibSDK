# Four Cross Mark Center Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Detect four corner cross Marks in a packed Mono8 image, return the diagonal intersection, and create a BGR diagnostic image.

**Architecture:** `MarkCenterModule` owns image segmentation, candidate selection, cross center extraction, diagonal intersection, and diagnostic drawing. `Wafer_FindFourCrossMarkCenter` is declared and implemented beside the current dark-frame C API; it wraps packed Mono8/BGR buffers as `cv::Mat` and copies only the calculated center to C callers. A sample loads the supplied image and saves the BGR result.

**Tech Stack:** C++17, OpenCV 4.1, CMake, MSVC.

## Global Constraints

- Inputs are only `CV_8UC1`; the C buffer is tightly packed `width * height` bytes.
- C output image is tightly packed BGR `width * height * 3` bytes.
- Return only the diagonal-intersection center numerically; Mark centers remain diagnostic-only in the image.
- C API declaration and definition are added beside `Wafer_CreateDarkFrameTemplate`.
- Existing dark-frame APIs remain unchanged.

---

### Task 1: Write mark-center regression tests

**Files:**
- Create: `tests/test_mark_center.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `MarkCenterModule::findFourCrossMarkCenter` and `Wafer_FindFourCrossMarkCenter`.
- Produces: `test_mark_center`, which exits zero only when C++ and C paths return `(205, 205)` for a four-cross synthetic Mono8 image.

- [ ] **Step 1: Write the failing test**

```cpp
cv::Mat image(400, 400, CV_8UC1, cv::Scalar(220));
drawCross(image, {60, 60});
drawCross(image, {340, 70});
drawCross(image, {330, 330});
drawCross(image, {70, 340});

wafer_calib::Point2D center;
cv::Mat diagnostic;
const auto status = wafer_calib::MarkCenterModule::findFourCrossMarkCenter(
    image, center, diagnostic);
if (!status.ok() || std::abs(center.x - 205.0) >= 0.5 ||
    std::abs(center.y - 205.0) >= 0.5 || diagnostic.type() != CV_8UC3) {
    return 1;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Build and run `test_mark_center`. Expected: compile failure because `MarkCenterModule` and `Wafer_FindFourCrossMarkCenter` are absent.

- [ ] **Step 3: Register the test target**

```cmake
add_executable(test_mark_center tests/test_mark_center.cpp)
target_link_libraries(test_mark_center PRIVATE WaferCalibSDK ${OpenCV_LIBS})
```

### Task 2: Implement the C++ Mark-center module

**Files:**
- Create: `include/wafer_calib/modules/mark_center.hpp`
- Create: `src/modules/mark_center.cpp`
- Modify: `include/wafer_calib/wafer_calib.hpp`

**Interfaces:**
- Consumes: a single `CV_8UC1` Mat.
- Produces: `Status findFourCrossMarkCenter(const cv::Mat&, Point2D&, cv::Mat&)`.

- [ ] **Step 1: Implement validation and foreground segmentation**

```cpp
if (mono8.empty()) return Status::Error(ErrorCode::ImageEmpty, "输入图像为空");
if (mono8.type() != CV_8UC1) return Status::Error(ErrorCode::ImageFormatMismatch, "仅支持 CV_8UC1 图像");
cv::threshold(mono8, foreground, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
cv::morphologyEx(foreground, foreground, cv::MORPH_OPEN,
                 cv::getStructuringElement(cv::MORPH_RECT, {3, 3}));
```

- [ ] **Step 2: Implement quadrant cross selection and projection centers**

Select exactly one near-square, cross-shaped component in each image quadrant. Derive the local row and column center from the plateau of maximal binary projection, then map it to global coordinates in TL/TR/BR/BL order.

- [ ] **Step 3: Implement diagonal intersection and diagnostic drawing**

Intersect lines `TL-BR` and `TR-BL`; return `FittingFailed` for a near-zero denominator. Convert to BGR and draw all four internal centers, both diagonals, the calculated center, the image center, and `dx/dy` text.

- [ ] **Step 4: Run the C++ test**

Expected: C++ path returns `(205, 205)` and a `CV_8UC3` diagnostic image.

### Task 3: Add the packed Mono8 C API and sample

**Files:**
- Modify: `include/wafer_calib/c_api/wafer_calib_c.h`
- Modify: `src/c_api/wafer_calib_c.cpp`
- Create: `samples/sample_mark_center.cpp`
- Modify: `tests/test_mark_center.cpp`

**Interfaces:**
- Consumes: `const unsigned char* image_buffer`, dimensions, output center pointers, and BGR output buffer.
- Produces: `Wafer_FindFourCrossMarkCenter(...)` and `sample_mark_center`.

- [ ] **Step 1: Add the C declaration and wrapper**

```c
WAFER_API int Wafer_FindFourCrossMarkCenter(
    const unsigned char* image_buffer, int width, int height,
    double* calculated_center_x, double* calculated_center_y,
    unsigned char* result_bgr);
```

Wrap input as `cv::Mat(height, width, CV_8UC1, ...)` and output as `cv::Mat(height, width, CV_8UC3, ...)`; call `MarkCenterModule` and copy the returned point.

- [ ] **Step 2: Extend the test through C API buffers**

Pass `image.data`, mutable `double center_x/center_y`, and a `400 * 400 * 3` byte BGR buffer. Require `WAFER_SUCCESS`, both coordinates within `0.5` of `205`, and at least one non-gray BGR pixel from the overlay.

- [ ] **Step 3: Implement the supplied-image sample**

Read `images/根据四个十字Mark求中心（兼容不同倍率，Mark大小不一样）/buffer-Mono8-4096x4096.bmp`, print the calculated center, and save `output_mark_center.bmp` next to it using Unicode-safe I/O.

- [ ] **Step 4: Build and run verification**

Build Release targets `test_mark_center` and `sample_mark_center`. Run the test, then run the sample from `build/Release`; confirm the saved image exists and inspect it visually.
