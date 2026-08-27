# Horizontal Line Angle Design

Input is one packed `CV_8UC1` image. The detector finds dark elongated horizontal calibration bars, estimates each bar's PCA major-axis direction, and returns the length-weighted circular mean. The angle is measured from the positive image X axis: a line descending as X increases is positive; output is normalized to `[-90, 90)` degrees.

```cpp
Status LineAngleModule::findHorizontalLineAngle(
    const cv::Mat& mono8, double& angle_degrees, cv::Mat& result_bgr);
```

```c
WAFER_API int Wafer_FindHorizontalLineAngle(
    const unsigned char* image_buffer, int width, int height,
    double* angle_degrees, unsigned char* result_bgr);
```

The C API lives beside `Wafer_CreateDarkFrameTemplate`. Input is `width * height` bytes and the BGR result is `width * height * 3` bytes. The result image draws every accepted bar centerline, the fused line, and the signed angle. Non-Mono8 inputs return `ImageFormatMismatch`; no qualifying bar returns `FeatureNotFound`.
