#include <cmath>
#include <cstdint>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "wafer_calib/c_api/wafer_calib_c.h"
#include "wafer_calib/modules/mark_center.hpp"

namespace {

void drawCross(cv::Mat& image, const cv::Point& center) {
    constexpr int arm_half_length = 30;
    constexpr int arm_half_thickness = 7;
    cv::rectangle(image,
                  {center.x - arm_half_length, center.y - arm_half_thickness},
                  {center.x + arm_half_length, center.y + arm_half_thickness},
                  cv::Scalar(10), cv::FILLED);
    cv::rectangle(image,
                  {center.x - arm_half_thickness, center.y - arm_half_length},
                  {center.x + arm_half_thickness, center.y + arm_half_length},
                  cv::Scalar(10), cv::FILLED);
}

bool hasColorOverlay(const cv::Mat& bgr) {
    for (int row = 0; row < bgr.rows; ++row) {
        const auto* pixels = bgr.ptr<cv::Vec3b>(row);
        for (int col = 0; col < bgr.cols; ++col) {
            if (pixels[col][0] != pixels[col][1] || pixels[col][1] != pixels[col][2]) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

int main() {
    cv::Mat image(400, 400, CV_8UC1, cv::Scalar(220));
    drawCross(image, {60, 60});
    drawCross(image, {340, 70});
    drawCross(image, {330, 330});
    drawCross(image, {70, 340});

    wafer_calib::Point2D cpp_center;
    cv::Mat cpp_result;
    const wafer_calib::Status cpp_status = wafer_calib::MarkCenterModule::findFourCrossMarkCenter(
        image, cpp_center, cpp_result);
    if (!cpp_status.ok() || std::abs(cpp_center.x - 205.0) >= 0.5 ||
        std::abs(cpp_center.y - 205.0) >= 0.5 || cpp_result.type() != CV_8UC3 ||
        !hasColorOverlay(cpp_result)) {
        return 1;
    }

    std::vector<unsigned char> c_result_buffer(static_cast<size_t>(image.total()) * 3U);
    double c_center_x = 0.0;
    double c_center_y = 0.0;
    const int c_status = Wafer_FindFourCrossMarkCenter(
        image.data, image.cols, image.rows, &c_center_x, &c_center_y, c_result_buffer.data());
    cv::Mat c_result(image.rows, image.cols, CV_8UC3, c_result_buffer.data());
    if (c_status != WAFER_SUCCESS || std::abs(c_center_x - 205.0) >= 0.5 ||
        std::abs(c_center_y - 205.0) >= 0.5 || !hasColorOverlay(c_result)) {
        return 2;
    }

    cv::Mat mono16(10, 10, CV_16UC1, cv::Scalar(0));
    if (wafer_calib::MarkCenterModule::findFourCrossMarkCenter(mono16, cpp_center, cpp_result).code !=
        wafer_calib::ErrorCode::ImageFormatMismatch) {
        return 3;
    }

    return 0;
}
