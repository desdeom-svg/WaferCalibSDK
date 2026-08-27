#include "wafer_calib/modules/line_angle.hpp"

#include <cmath>
#include <vector>

namespace wafer_calib {
namespace {

struct LineCandidate {
    cv::Vec4f fit_line;
    cv::Point2d center;
    double angle_degrees = 0.0;
    double weight = 0.0;
};

double normalizeLineAngle(double angle_degrees) {
    while (angle_degrees >= 90.0) angle_degrees -= 180.0;
    while (angle_degrees < -90.0) angle_degrees += 180.0;
    return angle_degrees;
}

void drawFittedLine(cv::Mat& result_bgr, const LineCandidate& candidate, const cv::Scalar& color) {
    const cv::Point2d direction(candidate.fit_line[0], candidate.fit_line[1]);
    const double length = std::hypot(result_bgr.cols, result_bgr.rows);
    const cv::Point start = candidate.center - direction * length;
    const cv::Point end = candidate.center + direction * length;
    cv::line(result_bgr, start, end, color, 2, cv::LINE_AA);
}

bool touchesVerticalImageBorder(const cv::Rect& region, const cv::Size& image_size) {
    // Horizontal calibration lines may extend beyond the left/right field of view.
    // Only top/bottom clipping truncates their cross-section and biases the fitted axis.
    return region.y == 0 || region.y + region.height == image_size.height;
}

} // namespace

Status LineAngleModule::findHorizontalLineAngle(
    const cv::Mat& mono8,
    double& angle_degrees,
    cv::Mat& result_bgr
) {
    if (mono8.empty()) {
        return Status::Error(ErrorCode::ImageEmpty, "输入图像为空");
    }
    if (mono8.type() != CV_8UC1) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "仅支持 CV_8UC1 图像");
    }

    cv::Mat foreground;
    cv::threshold(mono8, foreground, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
    cv::morphologyEx(
        foreground,
        foreground,
        cv::MORPH_OPEN,
        cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));

    cv::Mat labels;
    cv::Mat statistics;
    cv::Mat centroids;
    const int label_count = cv::connectedComponentsWithStats(
        foreground, labels, statistics, centroids, 8, CV_32S);

    const int minimum_width = mono8.cols / 3;
    std::vector<LineCandidate> candidates;
    std::vector<cv::Rect> discarded_border_regions;
    for (int label = 1; label < label_count; ++label) {
        const int width = statistics.at<int>(label, cv::CC_STAT_WIDTH);
        const int height = statistics.at<int>(label, cv::CC_STAT_HEIGHT);
        const int area = statistics.at<int>(label, cv::CC_STAT_AREA);
        if (width < minimum_width || width < height * 4 || area < width * height / 4) {
            continue;
        }

        const int left = statistics.at<int>(label, cv::CC_STAT_LEFT);
        const int top = statistics.at<int>(label, cv::CC_STAT_TOP);
        const cv::Rect region(left, top, width, height);
        if (touchesVerticalImageBorder(region, mono8.size())) {
            discarded_border_regions.push_back(region);
            continue;
        }

        std::vector<cv::Point> points;
        points.reserve(area);
        for (int row = top; row < top + height; ++row) {
            const int* label_row = labels.ptr<int>(row) + left;
            for (int column = 0; column < width; ++column) {
                if (label_row[column] == label) {
                    points.emplace_back(left + column, row);
                }
            }
        }
        if (points.size() < 2) {
            continue;
        }

        cv::Vec4f fitted_line;
        cv::fitLine(points, fitted_line, cv::DIST_L2, 0.0, 0.01, 0.01);
        const double candidate_angle = normalizeLineAngle(
            std::atan2(fitted_line[1], fitted_line[0]) * 180.0 / CV_PI);
        candidates.push_back({
            fitted_line,
            cv::Point2d(fitted_line[2], fitted_line[3]),
            candidate_angle,
            static_cast<double>(width)});
    }

    if (candidates.empty()) {
        return Status::Error(ErrorCode::FeatureNotFound, "未找到足够长的水平标定线");
    }

    double weighted_cosine = 0.0;
    double weighted_sine = 0.0;
    cv::Point2d weighted_center(0.0, 0.0);
    double total_weight = 0.0;
    for (const LineCandidate& candidate : candidates) {
        const double double_angle_radians = candidate.angle_degrees * 2.0 * CV_PI / 180.0;
        weighted_cosine += candidate.weight * std::cos(double_angle_radians);
        weighted_sine += candidate.weight * std::sin(double_angle_radians);
        weighted_center += candidate.weight * candidate.center;
        total_weight += candidate.weight;
    }
    angle_degrees = normalizeLineAngle(
        std::atan2(weighted_sine, weighted_cosine) * 90.0 / CV_PI);
    weighted_center *= 1.0 / total_weight;

    cv::cvtColor(mono8, result_bgr, cv::COLOR_GRAY2BGR);
    for (const cv::Rect& region : discarded_border_regions) {
        cv::rectangle(result_bgr, region, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
    }
    for (const LineCandidate& candidate : candidates) {
        drawFittedLine(result_bgr, candidate, cv::Scalar(0, 255, 0));
    }
    const double angle_radians = angle_degrees * CV_PI / 180.0;
    const LineCandidate fused_line{
        cv::Vec4f(static_cast<float>(std::cos(angle_radians)), static_cast<float>(std::sin(angle_radians)),
                  static_cast<float>(weighted_center.x), static_cast<float>(weighted_center.y)),
        weighted_center,
        angle_degrees,
        1.0};
    drawFittedLine(result_bgr, fused_line, cv::Scalar(0, 255, 255));
    cv::putText(result_bgr, cv::format("angle=%.4f deg", angle_degrees), cv::Point(24, 40),
                cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
    return Status::OK();
}

} // namespace wafer_calib
