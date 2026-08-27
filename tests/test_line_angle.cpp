#include <cmath>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "wafer_calib/c_api/wafer_calib_c.h"
#include "wafer_calib/modules/line_angle.hpp"

namespace {

bool hasColorOverlay(const cv::Mat& bgr) {
    for (int row = 0; row < bgr.rows; ++row) {
        const auto* pixels = bgr.ptr<cv::Vec3b>(row);
        for (int column = 0; column < bgr.cols; ++column) {
            if (pixels[column][0] != pixels[column][1] || pixels[column][1] != pixels[column][2]) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

int main() {
    cv::Mat image(300, 400, CV_8UC1, cv::Scalar(220));
    cv::line(image, {20, 100}, {380, 118}, cv::Scalar(10), 24, cv::LINE_8);
    const double expected_angle = std::atan2(18.0, 360.0) * 180.0 / CV_PI;

    double cpp_angle = 0.0;
    cv::Mat cpp_result;
    const wafer_calib::Status cpp_status = wafer_calib::LineAngleModule::findHorizontalLineAngle(
        image, cpp_angle, cpp_result);
    if (!cpp_status.ok() || std::abs(cpp_angle - expected_angle) >= 0.2 ||
        cpp_result.type() != CV_8UC3 || !hasColorOverlay(cpp_result)) {
        return 1;
    }

    std::vector<unsigned char> result_buffer(static_cast<size_t>(image.total()) * 3U);
    double c_angle = 0.0;
    const int c_status = Wafer_FindHorizontalLineAngle(
        image.data, image.cols, image.rows, &c_angle, result_buffer.data());
    cv::Mat c_result(image.rows, image.cols, CV_8UC3, result_buffer.data());
    if (c_status != WAFER_SUCCESS || std::abs(c_angle - expected_angle) >= 0.2 ||
        !hasColorOverlay(c_result)) {
        return 2;
    }

    // A horizontal calibration line may span the left and right image borders.
    cv::Mat side_touching_line_image(300, 400, CV_8UC1, cv::Scalar(220));
    cv::line(side_touching_line_image, {0, 100}, {399, 120}, cv::Scalar(10), 24, cv::LINE_8);
    const double side_touching_expected_angle = std::atan2(20.0, 399.0) * 180.0 / CV_PI;

    double side_touching_line_angle = 0.0;
    cv::Mat side_touching_line_result;
    const wafer_calib::Status side_touching_line_status =
        wafer_calib::LineAngleModule::findHorizontalLineAngle(
            side_touching_line_image, side_touching_line_angle, side_touching_line_result);
    if (!side_touching_line_status.ok() ||
        std::abs(side_touching_line_angle - side_touching_expected_angle) >= 0.2) {
        return 3;
    }

    // A line clipped by the bottom image border must not bias the angle of a complete line.
    cv::Mat clipped_line_image(300, 400, CV_8UC1, cv::Scalar(220));
    cv::line(clipped_line_image, {20, 80}, {380, 98}, cv::Scalar(10), 24, cv::LINE_8);
    cv::line(clipped_line_image, {20, 250}, {380, 310}, cv::Scalar(10), 24, cv::LINE_8);

    double clipped_line_angle = 0.0;
    cv::Mat clipped_line_result;
    const wafer_calib::Status clipped_line_status =
        wafer_calib::LineAngleModule::findHorizontalLineAngle(
            clipped_line_image, clipped_line_angle, clipped_line_result);
    if (!clipped_line_status.ok() || std::abs(clipped_line_angle - expected_angle) >= 0.2) {
        return 4;
    }
    return 0;
}
