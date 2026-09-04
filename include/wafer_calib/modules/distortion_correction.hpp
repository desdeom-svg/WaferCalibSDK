#ifndef WAFER_CALIB_MODULES_DISTORTION_CORRECTION_HPP_
#define WAFER_CALIB_MODULES_DISTORTION_CORRECTION_HPP_

#include <array>
#include <vector>

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

enum class CalibrationViewPosition {
    Center = 0,     // 中心视野
    TopLeft = 1,    // 左上视野
    BottomLeft = 2, // 左下视野
    TopRight = 3,   // 右上视野
    BottomRight = 4 // 右下视野
};

struct CalibrationViewInput {
    CalibrationViewPosition position = CalibrationViewPosition::Center;
    cv::Mat image_mono8;
    double stage_offset_x_mm = 0.0;
    double stage_offset_y_mm = 0.0;
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

    static Status createMultiViewDotGridTemplate(
        const std::vector<CalibrationViewInput>& views,
        int grid_columns,
        int grid_rows,
        double point_spacing_mm,
        double stage_step_mm,
        DotGridDistortionTemplate& output_template,
        cv::Mat& diagnostic_bgr);

    static Status correctByDotGridTemplate(
        const cv::Mat& mono8,
        const DotGridDistortionTemplate& distortion_template,
        cv::Mat& corrected_mono8);

    static Status saveTemplateToFile(
        const std::string& file_path,
        const DotGridDistortionTemplate& distortion_template);

    static Status loadTemplateFromFile(
        const std::string& file_path,
        DotGridDistortionTemplate& distortion_template);
};

} // namespace wafer_calib

#endif // WAFER_CALIB_MODULES_DISTORTION_CORRECTION_HPP_
