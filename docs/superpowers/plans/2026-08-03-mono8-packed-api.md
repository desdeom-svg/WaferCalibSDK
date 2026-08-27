# Mono8 Packed C API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Simplify the original dark-frame C API to accept only tightly packed `CV_8UC1` buffers, so every row uses `step == width` internally.

**Architecture:** Keep `NoiseReductionModule::createDarkFrameTemplate` unchanged as the C++ image-level API. Change the exported in-memory C API by removing its `step` argument and constructing each input/output `cv::Mat` with the packed row stride implied by `width`; the file API remains unchanged. A native executable regression test sends real byte buffers through the exported C API and verifies the median output.

**Tech Stack:** C++17, OpenCV 4.10, CMake, MSVC.

## Global Constraints

- The simplified in-memory C API supports only tightly packed `CV_8UC1` data.
- `frame_buffers[i]` and `out_dark_template` must each address at least `width * height` bytes.
- `width`, `height`, and `frame_count` must be positive.
- The existing file-path C API and C++ API remain source-compatible.
- No build artifact or image asset is changed by the source patch.

---

### Task 1: Add the packed Mono8 C API regression test

**Files:**
- Create: `tests/test_c_api_packed_mono8.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Wafer_CreateDarkFrameTemplate(const unsigned char* const*, int, unsigned char*, int, int, int)`.
- Produces: `test_c_api_packed_mono8`, a native executable returning zero only when the C API computes the expected per-pixel median from contiguous Mono8 buffers.

- [ ] **Step 1: Write the failing test**

```cpp
const unsigned char frame0[] = {1, 100, 3, 4};
const unsigned char frame1[] = {2,  20, 4, 8};
const unsigned char frame2[] = {3,  30, 5, 6};
const unsigned char* frames[] = {frame0, frame1, frame2};
unsigned char output[] = {0, 0, 0, 0};
const int result = Wafer_CreateDarkFrameTemplate(
    frames, 3, output, 2, 2, 1 /* Median */);
assert(result == WAFER_SUCCESS);
assert((std::vector<unsigned char>(output, output + 4) ==
        std::vector<unsigned char>{2, 30, 4, 6}));
```

- [ ] **Step 2: Run the test to verify it fails**

Run the configured test executable after building it. Expected: compilation fails because the header still requires the removed `step` argument.

- [ ] **Step 3: Register the test in CMake**

```cmake
add_executable(test_c_api_packed_mono8 tests/test_c_api_packed_mono8.cpp)
target_link_libraries(test_c_api_packed_mono8 PRIVATE WaferCalibSDK)
```

- [ ] **Step 4: Run the test to verify the expected API failure**

Build `test_c_api_packed_mono8` with the existing Visual Studio CMake generator. Expected: the C++ compiler reports the old function signature has too few arguments.

### Task 2: Simplify the exported Mono8 API

**Files:**
- Modify: `include/wafer_calib/c_api/wafer_calib_c.h`
- Modify: `src/c_api/wafer_calib_c.cpp`
- Modify: `samples/sample_noise_reduction.cpp`

**Interfaces:**
- Consumes: packed Mono8 buffers of `width * height` bytes.
- Produces: `Wafer_CreateDarkFrameTemplate(frame_buffers, frame_count, out_dark_template, width, height, method)`.

- [ ] **Step 1: Change the public declaration and documentation**

```c
WAFER_API int Wafer_CreateDarkFrameTemplate(
    const unsigned char** frame_buffers,
    int frame_count,
    unsigned char* out_dark_template,
    int width,
    int height,
    int method);
```

- [ ] **Step 2: Construct packed matrices in the implementation**

```cpp
cv::Mat frame(height, width, CV_8UC1,
              const_cast<unsigned char*>(frame_buffers[i]));
cv::Mat out_mat(height, width, CV_8UC1, out_dark_template);
```

- [ ] **Step 3: Update the sample C API invocation**

The sample continues to exercise the file-path C API and requires no in-memory stride argument.

- [ ] **Step 4: Run the regression test to verify it passes**

Run `test_c_api_packed_mono8`. Expected: exit code zero and no assertion failure.

### Task 3: Build and verify sample behavior

**Files:**
- Modify: `samples/sample_noise_reduction.cpp`

**Interfaces:**
- Consumes: the repository's `images/去噪声/背景噪声*.bmp` assets when invoked from the repository root.
- Produces: nonzero exit on absent input frames and explicit diagnostics; normal output images only after successful C++ and C API calls.

- [ ] **Step 1: Add the failing process-level regression check**

Invoke the current sample from `build/Release`. Expected: it reports zero loaded frames but exits zero, which demonstrates the incorrect success condition.

- [ ] **Step 2: Make no-frame loading an explicit failure**

```cpp
if (dark_frames.empty()) {
    std::cerr << "No dark frames could be loaded." << std::endl;
    return 1;
}
```

- [ ] **Step 3: Build the sample and run it from the repository root**

Expected: 10 images are loaded and both template outputs are written.

- [ ] **Step 4: Run the sample from `build/Release`**

Expected: nonzero exit with the missing-input diagnostic, never false success.
