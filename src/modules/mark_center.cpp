#include "wafer_calib/modules/mark_center.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace wafer_calib {
namespace {

enum class CornerIndex : int {
    TopLeft = 0,
    TopRight = 1,
    BottomRight = 2,
    BottomLeft = 3,
};

struct CrossCandidate {
    Point2D center;
    double score = 0.0;
};

double crossProduct(const cv::Point2d& first, const cv::Point2d& second) {
    return first.x * second.y - first.y * second.x;
}

int plateauCenter(const std::vector<int>& projection) {
    const int maximum = *std::max_element(projection.begin(), projection.end());
    const int minimum_on_plateau = std::max(1, static_cast<int>(std::ceil(maximum * 0.98)));

    int sum = 0;
    int count = 0;
    for (int index = 0; index < static_cast<int>(projection.size()); ++index) {
        if (projection[index] >= minimum_on_plateau) {
            sum += index;
            ++count;
        }
    }
    return count == 0 ? 0 : cvRound(static_cast<double>(sum) / count);
}

bool findCandidateCenter(
    const cv::Mat& labels,
    int label,
    const cv::Rect& box,
    Point2D& center,
    double& score
) {
    std::vector<int> row_projection(box.height, 0);
    std::vector<int> column_projection(box.width, 0);
    int area = 0;

    for (int row = 0; row < box.height; ++row) {
        const int* label_row = labels.ptr<int>(box.y + row) + box.x;
        for (int column = 0; column < box.width; ++column) {
            if (label_row[column] != label) {
                continue;
            }
            ++row_projection[row];
            ++column_projection[column];
            ++area;
        }
    }

    const int row_peak = *std::max_element(row_projection.begin(), row_projection.end());
    const int column_peak = *std::max_element(column_projection.begin(), column_projection.end());
    const double fill_ratio = static_cast<double>(area) / static_cast<double>(box.area());
    const double aspect_ratio = static_cast<double>(std::max(box.width, box.height)) /
                                static_cast<double>(std::min(box.width, box.height));
    const double horizontal_strength = static_cast<double>(row_peak) / box.width;
    const double vertical_strength = static_cast<double>(column_peak) / box.height;

    if (aspect_ratio > 1.8 || fill_ratio < 0.12 || fill_ratio > 0.68 ||
        horizontal_strength < 0.75 || vertical_strength < 0.75) {
        return false;
    }

    center.x = box.x + plateauCenter(column_projection);
    center.y = box.y + plateauCenter(row_projection);
    score = std::min(horizontal_strength, vertical_strength) * (1.0 - std::abs(1.0 - fill_ratio));
    return true;
}

CornerIndex cornerForPoint(const Point2D& point, const cv::Size& image_size) {
    const bool is_left = point.x < image_size.width / 2.0;
    const bool is_top = point.y < image_size.height / 2.0;
    if (is_top && is_left) return CornerIndex::TopLeft;
    if (is_top) return CornerIndex::TopRight;
    if (is_left) return CornerIndex::BottomLeft;
    return CornerIndex::BottomRight;
}

Status findFourCrossCenters(const cv::Mat& mono8, std::array<Point2D, 4>& centers) {
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

    const int minimum_area = std::max(100, static_cast<int>(mono8.total() * 0.00005));
    std::array<CrossCandidate, 4> candidates;
    std::array<bool, 4> has_candidate = {false, false, false, false};

    for (int label = 1; label < label_count; ++label) {
        const int area = statistics.at<int>(label, cv::CC_STAT_AREA);
        if (area < minimum_area) {
            continue;
        }

        const cv::Rect box(
            statistics.at<int>(label, cv::CC_STAT_LEFT),
            statistics.at<int>(label, cv::CC_STAT_TOP),
            statistics.at<int>(label, cv::CC_STAT_WIDTH),
            statistics.at<int>(label, cv::CC_STAT_HEIGHT));
        if (box.width == 0 || box.height == 0) {
            continue;
        }

        Point2D center;
        double score = 0.0;
        if (!findCandidateCenter(labels, label, box, center, score)) {
            continue;
        }

        const int corner_index = static_cast<int>(cornerForPoint(center, mono8.size()));
        if (!has_candidate[corner_index] || score > candidates[corner_index].score) {
            candidates[corner_index] = {center, score};
            has_candidate[corner_index] = true;
        }
    }

    if (std::any_of(has_candidate.begin(), has_candidate.end(), [](bool found) { return !found; })) {
        return Status::Error(ErrorCode::FeatureNotFound, "未能在四个象限中各找到一个十字 Mark");
    }

    centers[static_cast<int>(CornerIndex::TopLeft)] = candidates[static_cast<int>(CornerIndex::TopLeft)].center;
    centers[static_cast<int>(CornerIndex::TopRight)] = candidates[static_cast<int>(CornerIndex::TopRight)].center;
    centers[static_cast<int>(CornerIndex::BottomRight)] = candidates[static_cast<int>(CornerIndex::BottomRight)].center;
    centers[static_cast<int>(CornerIndex::BottomLeft)] = candidates[static_cast<int>(CornerIndex::BottomLeft)].center;
    return Status::OK();
}

Status intersectDiagonals(const std::array<Point2D, 4>& centers, Point2D& intersection) {
    const cv::Point2d top_left = centers[static_cast<int>(CornerIndex::TopLeft)].toCvPoint();
    const cv::Point2d top_right = centers[static_cast<int>(CornerIndex::TopRight)].toCvPoint();
    const cv::Point2d bottom_right = centers[static_cast<int>(CornerIndex::BottomRight)].toCvPoint();
    const cv::Point2d bottom_left = centers[static_cast<int>(CornerIndex::BottomLeft)].toCvPoint();
    const cv::Point2d first_direction = bottom_right - top_left;
    const cv::Point2d second_direction = bottom_left - top_right;
    const double denominator = crossProduct(first_direction, second_direction);
    if (std::abs(denominator) < 1e-8) {
        return Status::Error(ErrorCode::FittingFailed, "两条对角线平行或退化，无法求交点");
    }

    const double first_scale = crossProduct(top_right - top_left, second_direction) / denominator;
    const cv::Point2d center = top_left + first_scale * first_direction;
    intersection = Point2D(center);
    return Status::OK();
}

void drawDiagnostic(
    const cv::Mat& mono8,
    const std::array<Point2D, 4>& mark_centers,
    const Point2D& calculated_center,
    cv::Mat& result_bgr
) {
    cv::cvtColor(mono8, result_bgr, cv::COLOR_GRAY2BGR);
    const std::array<std::string, 4> labels = {"TL", "TR", "BR", "BL"};
    const cv::Scalar mark_color(0, 255, 0);

    for (int index = 0; index < 4; ++index) {
        const cv::Point point(cvRound(mark_centers[index].x), cvRound(mark_centers[index].y));
        cv::drawMarker(result_bgr, point, mark_color, cv::MARKER_CROSS, 36, 2, cv::LINE_AA);
        cv::putText(result_bgr, labels[index], point + cv::Point(12, -12),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, mark_color, 2, cv::LINE_AA);
    }

    const cv::Point top_left(cvRound(mark_centers[0].x), cvRound(mark_centers[0].y));
    const cv::Point top_right(cvRound(mark_centers[1].x), cvRound(mark_centers[1].y));
    const cv::Point bottom_right(cvRound(mark_centers[2].x), cvRound(mark_centers[2].y));
    const cv::Point bottom_left(cvRound(mark_centers[3].x), cvRound(mark_centers[3].y));
    cv::line(result_bgr, top_left, bottom_right, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
    cv::line(result_bgr, top_right, bottom_left, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);

    const Point2D image_center((mono8.cols - 1) / 2.0, (mono8.rows - 1) / 2.0);
    const cv::Point calculated_point(cvRound(calculated_center.x), cvRound(calculated_center.y));
    const cv::Point image_point(cvRound(image_center.x), cvRound(image_center.y));
    cv::drawMarker(result_bgr, calculated_point, cv::Scalar(0, 0, 255), cv::MARKER_TILTED_CROSS, 48, 3, cv::LINE_AA);
    cv::drawMarker(result_bgr, image_point, cv::Scalar(255, 0, 255), cv::MARKER_DIAMOND, 36, 2, cv::LINE_AA);
    cv::line(result_bgr, calculated_point, image_point, cv::Scalar(255, 0, 0), 2, cv::LINE_AA);

    const double offset_x = calculated_center.x - image_center.x;
    const double offset_y = calculated_center.y - image_center.y;
    cv::putText(result_bgr, cv::format("center=(%.2f, %.2f)", calculated_center.x, calculated_center.y),
                cv::Point(24, 40), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
    cv::putText(result_bgr, cv::format("dx=%.2f dy=%.2f", offset_x, offset_y),
                cv::Point(24, 74), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 0, 0), 2, cv::LINE_AA);
}

} // namespace

Status MarkCenterModule::findFourCrossMarkCenter(
    const cv::Mat& mono8,
    Point2D& calculated_center,
    cv::Mat& result_bgr
) {
    if (mono8.empty()) {
        return Status::Error(ErrorCode::ImageEmpty, "输入图像为空");
    }
    if (mono8.type() != CV_8UC1) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "仅支持 CV_8UC1 图像");
    }

    std::array<Point2D, 4> mark_centers;
    Status status = findFourCrossCenters(mono8, mark_centers);
    if (!status.ok()) {
        return status;
    }

    status = intersectDiagonals(mark_centers, calculated_center);
    if (!status.ok()) {
        return status;
    }

    drawDiagnostic(mono8, mark_centers, calculated_center, result_bgr);
    return Status::OK();
}

} // namespace wafer_calib
