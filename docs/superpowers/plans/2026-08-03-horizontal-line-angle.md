# Horizontal Line Angle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Return the signed angle of dark horizontal calibration bars and a BGR diagnostic image.

**Architecture:** A C++ module thresholds the dark foreground, filters elongated connected components, applies `cv::fitLine` to each accepted component, and fuses their double-angle directions by component width. The C API wraps packed Mono8/BGR buffers; the sample processes both supplied calibration views.

**Tech Stack:** C++17, OpenCV 4.1, CMake, MSVC.

## Global Constraints

- Only packed `CV_8UC1` input is accepted.
- Positive angle means left-to-right downward tilt in image coordinates.
- Output angle is normalized to `[-90, 90)` degrees.
- The C declaration and implementation are adjacent to the existing dark-frame C API.

---

### Task 1: Add the failing synthetic regression test

**Files:**
- Create: `tests/test_line_angle.cpp`
- Modify: `CMakeLists.txt`

- [ ] Create a 400x300 Mono8 image with a dark bar from `(20, 100)` to `(380, 118)`, call both C++ and C interfaces, and require an angle within `0.2` degrees of `atan2(18, 360)` in degrees plus a color diagnostic overlay.
- [ ] Build the test before implementation and confirm the missing module/header compilation failure.

### Task 2: Implement module and C wrapper

**Files:**
- Create: `include/wafer_calib/modules/line_angle.hpp`
- Create: `src/modules/line_angle.cpp`
- Modify: `include/wafer_calib/wafer_calib.hpp`
- Modify: `include/wafer_calib/c_api/wafer_calib_c.h`
- Modify: `src/c_api/wafer_calib_c.cpp`

- [ ] Segment dark pixels with Otsu inverse threshold, filter components wider than one third of the image and at least four times wider than tall, fit directions with `cv::fitLine`, and fuse normalized directions with double-angle weighted averaging.
- [ ] Add `Wafer_FindHorizontalLineAngle` with packed Mono8 input, `double* angle_degrees`, and packed BGR output.
- [ ] Run the synthetic test and require exit code zero.

### Task 3: Add and verify supplied-image sample

**Files:**
- Create: `samples/sample_line_angle.cpp`

- [ ] Read `角度调整2.bmp` and `角度调整1.bmp`, print each signed angle, and write `output_angle_adjustment_2.bmp` and `output_angle_adjustment_1.bmp` beside them.
- [ ] Build Release targets, run both existing and new regressions, run the sample from `build/Release`, and inspect the result images.
