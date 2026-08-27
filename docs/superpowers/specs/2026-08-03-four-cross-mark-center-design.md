# Four Cross Mark Center Design

## Goal

For one packed `CV_8UC1` image, locate the four corner cross Mark centers, calculate the intersection of the two diagonals, compare it with the image geometric center, and return a BGR diagnostic image.

## Scope and Constraints

- Input is only tightly packed Mono8 data: `width * height` bytes; no stride parameter.
- Mark centers are detected internally in this fixed order: top-left, top-right, bottom-right, bottom-left. They are drawn in the result image but are not returned by either public interface.
- The calculated center is the intersection of the top-left to bottom-right and top-right to bottom-left diagonals, not the arithmetic mean.
- The image geometric center is `((width - 1) / 2.0, (height - 1) / 2.0)` in OpenCV pixel-center coordinates.
- The result image is packed BGR, `width * height * 3` bytes.
- Existing dark-frame APIs are unchanged. The new C declaration and implementation live beside `Wafer_CreateDarkFrameTemplate` in `wafer_calib_c.h` and `wafer_calib_c.cpp`.

## Public Interfaces

### C++ module

```cpp
class MarkCenterModule {
public:
    static Status findFourCrossMarkCenter(
        const cv::Mat& mono8,
        Point2D& calculated_center,
        cv::Mat& result_bgr);
};
```

`mono8` must be `CV_8UC1`. `result_bgr` is `CV_8UC3` and has the same width and height as the input.

### C API

```c
WAFER_API int Wafer_FindFourCrossMarkCenter(
    const unsigned char* image_buffer,
    int width,
    int height,
    double* calculated_center_x,
    double* calculated_center_y,
    unsigned char* result_bgr);
```

`image_buffer` addresses `width * height` bytes. `result_bgr` addresses `width * height * 3` bytes. Only the calculated center is returned numerically; the four Mark centers and the image-center offset are diagnostic annotations in `result_bgr`. The C function maps the C++ status to the existing `WAFER_*` error codes.

## Detection and Center Calculation

1. Validate a non-empty `CV_8UC1` image.
2. Segment the dark foreground with Otsu inverse thresholding and remove small isolated noise with a 3x3 morphological opening.
3. Extract connected components. Reject components that are too small relative to image area, strongly non-square, or have an implausible foreground fill ratio.
4. Consider candidates in each image quadrant. Score each by the strength and balance of its longest horizontal and vertical projections; select one cross candidate in every quadrant.
5. Calculate each cross center as the intersection of its horizontal and vertical projection plateaus. This uses the Mark geometry rather than a fixed pixel size.
6. Intersect the `TL-BR` and `TR-BL` diagonal lines. Return `FittingFailed` if the lines are parallel or numerically degenerate.
7. Convert the gray image to BGR; draw four labeled cross centers, both diagonals, the calculated center, the image center, and the `dx/dy` offset.

## Errors and Verification

- Non-Mono8 input: `ImageFormatMismatch`.
- Missing quadrant candidate or an ambiguous selection: `FeatureNotFound`.
- Degenerate diagonals: `FittingFailed`.
- Native regression tests use a synthetic Mono8 image with four known crosses to verify the diagonal intersection, BGR output, and C API buffer writes.
- `sample_mark_center` reads `buffer-Mono8-4096x4096.bmp`, prints the calculated center, and writes `output_mark_center.bmp` beside the input image.
