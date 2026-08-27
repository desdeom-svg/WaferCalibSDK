#ifndef WAFER_CALIB_MODULES_DISTORTION_CORRECTION_HPP_
#define WAFER_CALIB_MODULES_DISTORTION_CORRECTION_HPP_

#include <array>

#include "wafer_calib/core/status.hpp"
#include "wafer_calib/core/types.hpp"

namespace wafer_calib {

constexpr int kDotGridPolynomialCoefficientCount = 10;

struct DotGridDistortionTemplate {
    int image_width = 0;
    int image_height = 0;
    std::array<double, kDotGridPolynomialCoefficientCount> output_to_input_x{};
    std::array<double, kDotGridPolynomialCoefficientCount> output_to_input_y{};
    int detected_point_count = 0;
    double rms_error_pixels = 0.0;
};

class DistortionCorrectionModule {
public:
    static Status createDotGridTemplate(
        const cv::Mat& mono8,
        int grid_columns,
        int grid_rows,
        double point_spacing_mm,
        DotGridDistortionTemplate& output_template,
        cv::Mat& result_bgr);

    static Status correctByDotGridTemplate(
        const cv::Mat& mono8,
        const DotGridDistortionTemplate& distortion_template,
        cv::Mat& corrected_mono8);
};

} // namespace wafer_calib

#endif // WAFER_CALIB_MODULES_DISTORTION_CORRECTION_HPP_
