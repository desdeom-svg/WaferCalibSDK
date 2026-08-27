#include "wafer_calib/modules/distortion_correction.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

#include <opencv2/calib3d.hpp>

namespace wafer_calib {
namespace {

cv::Ptr<cv::FeatureDetector> createDarkCircleDetector(const cv::Size& image_size) {
    const double minimum_dimension = static_cast<double>(std::min(image_size.width, image_size.height));

    cv::SimpleBlobDetector::Params params;
    params.minThreshold = 0.0F;
    params.maxThreshold = 255.0F;
    params.thresholdStep = 5.0F;
    params.filterByColor = true;
    params.blobColor = 0;
    params.filterByArea = true;
    params.minArea = static_cast<float>(std::max(20.0, minimum_dimension * minimum_dimension / 100000.0));
    params.maxArea = static_cast<float>(minimum_dimension * minimum_dimension / 20.0);
    params.filterByCircularity = true;
    params.minCircularity = 0.60F;
    params.filterByConvexity = false;
    params.filterByInertia = false;
    params.minDistBetweenBlobs = static_cast<float>(std::max(5.0, minimum_dimension / 100.0));
    return cv::SimpleBlobDetector::create(params);
}

struct AxisGroup {
    std::vector<double> values;
    double center = 0.0;
};

struct OrderedGridPoint {
    int row = 0;
    int column = 0;
    cv::Point2f source_center;
};

std::vector<AxisGroup> selectGridAxisGroups(
    const std::vector<cv::KeyPoint>& keypoints,
    bool use_x,
    int expected_group_count,
    double grouping_threshold
) {
    std::vector<double> values;
    values.reserve(keypoints.size());
    for (const cv::KeyPoint& keypoint : keypoints) {
        values.push_back(use_x ? keypoint.pt.x : keypoint.pt.y);
    }
    std::sort(values.begin(), values.end());

    std::vector<AxisGroup> groups;
    for (double value : values) {
        if (groups.empty() || value - groups.back().values.back() > grouping_threshold) {
            groups.push_back({});
        }
        groups.back().values.push_back(value);
    }
    for (AxisGroup& group : groups) {
        group.center = std::accumulate(group.values.begin(), group.values.end(), 0.0) / group.values.size();
    }
    if (static_cast<int>(groups.size()) < expected_group_count) {
        return {};
    }

    std::sort(groups.begin(), groups.end(), [](const AxisGroup& left, const AxisGroup& right) {
        return left.values.size() > right.values.size();
    });
    groups.resize(expected_group_count);
    std::sort(groups.begin(), groups.end(), [](const AxisGroup& left, const AxisGroup& right) {
        return left.center < right.center;
    });
    return groups;
}

std::vector<OrderedGridPoint> orderFixedViewDotGrid(
    const std::vector<cv::KeyPoint>& keypoints,
    const cv::Size& image_size,
    int grid_columns,
    int grid_rows
) {
    const double grouping_threshold = std::max(
        3.0,
        static_cast<double>(std::min(image_size.width, image_size.height)) /
            (std::max(grid_columns, grid_rows) * 5.0));
    const std::vector<AxisGroup> column_groups =
        selectGridAxisGroups(keypoints, true, grid_columns, grouping_threshold);
    const std::vector<AxisGroup> row_groups =
        selectGridAxisGroups(keypoints, false, grid_rows, grouping_threshold);
    if (column_groups.empty() || row_groups.empty()) {
        return {};
    }

    const int expected_point_count = grid_columns * grid_rows;
    std::vector<int> selected_keypoints(expected_point_count, -1);
    std::vector<double> assignment_distances(expected_point_count, std::numeric_limits<double>::infinity());
    for (int keypoint_index = 0; keypoint_index < static_cast<int>(keypoints.size()); ++keypoint_index) {
        const cv::Point2f point = keypoints[keypoint_index].pt;
        int nearest_column = 0;
        int nearest_row = 0;
        double column_distance = std::numeric_limits<double>::infinity();
        double row_distance = std::numeric_limits<double>::infinity();
        for (int column = 0; column < grid_columns; ++column) {
            const double distance = std::abs(point.x - column_groups[column].center);
            if (distance < column_distance) {
                column_distance = distance;
                nearest_column = column;
            }
        }
        for (int row = 0; row < grid_rows; ++row) {
            const double distance = std::abs(point.y - row_groups[row].center);
            if (distance < row_distance) {
                row_distance = distance;
                nearest_row = row;
            }
        }
        if (column_distance > grouping_threshold || row_distance > grouping_threshold) {
            continue;
        }

        const int grid_index = nearest_row * grid_columns + nearest_column;
        const double assignment_distance = column_distance + row_distance;
        if (assignment_distance < assignment_distances[grid_index]) {
            assignment_distances[grid_index] = assignment_distance;
            selected_keypoints[grid_index] = keypoint_index;
        }
    }

    std::vector<OrderedGridPoint> ordered_points;
    ordered_points.reserve(expected_point_count);
    for (int row = 0; row < grid_rows; ++row) {
        for (int column = 0; column < grid_columns; ++column) {
            const int grid_index = row * grid_columns + column;
            if (selected_keypoints[grid_index] >= 0) {
                ordered_points.push_back({row, column, keypoints[selected_keypoints[grid_index]].pt});
            }
        }
    }
    const int minimum_usable_points = std::max(4, static_cast<int>(std::ceil(expected_point_count * 0.90)));
    return static_cast<int>(ordered_points.size()) >= minimum_usable_points ? ordered_points : std::vector<OrderedGridPoint>{};
}

void fillPolynomialTerms(const cv::Point2d& point, const cv::Size& image_size, cv::Mat& terms, int row) {
    const double normalized_x = image_size.width > 1
        ? 2.0 * point.x / static_cast<double>(image_size.width - 1) - 1.0
        : 0.0;
    const double normalized_y = image_size.height > 1
        ? 2.0 * point.y / static_cast<double>(image_size.height - 1) - 1.0
        : 0.0;
    terms.at<double>(row, 0) = 1.0;
    terms.at<double>(row, 1) = normalized_x;
    terms.at<double>(row, 2) = normalized_y;
    terms.at<double>(row, 3) = normalized_x * normalized_x;
    terms.at<double>(row, 4) = normalized_x * normalized_y;
    terms.at<double>(row, 5) = normalized_y * normalized_y;
    terms.at<double>(row, 6) = normalized_x * normalized_x * normalized_x;
    terms.at<double>(row, 7) = normalized_x * normalized_x * normalized_y;
    terms.at<double>(row, 8) = normalized_x * normalized_y * normalized_y;
    terms.at<double>(row, 9) = normalized_y * normalized_y * normalized_y;
}

double evaluatePolynomial(
    const std::array<double, kDotGridPolynomialCoefficientCount>& coefficients,
    const cv::Point2d& point,
    const cv::Size& image_size
) {
    cv::Mat terms(1, kDotGridPolynomialCoefficientCount, CV_64F);
    fillPolynomialTerms(point, image_size, terms, 0);
    double value = 0.0;
    for (int index = 0; index < kDotGridPolynomialCoefficientCount; ++index) {
        value += coefficients[index] * terms.at<double>(0, index);
    }
    return value;
}

bool isFiniteTemplate(const DotGridDistortionTemplate& distortion_template) {
    for (double coefficient : distortion_template.output_to_input_x) {
        if (!std::isfinite(coefficient)) return false;
    }
    for (double coefficient : distortion_template.output_to_input_y) {
        if (!std::isfinite(coefficient)) return false;
    }
    return std::isfinite(distortion_template.rms_error_pixels);
}

} // namespace

Status DistortionCorrectionModule::createDotGridTemplate(
    const cv::Mat& mono8,
    int grid_columns,
    int grid_rows,
    double point_spacing_mm,
    DotGridDistortionTemplate& output_template,
    cv::Mat& result_bgr
) {
    if (mono8.empty()) {
        return Status::Error(ErrorCode::ImageEmpty, "input image is empty");
    }
    if (mono8.type() != CV_8UC1) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "only CV_8UC1 is supported");
    }
    if (grid_columns < 2 || grid_rows < 2 || !std::isfinite(point_spacing_mm) || point_spacing_mm <= 0.0) {
        return Status::Error(ErrorCode::InvalidParam, "invalid dot-grid dimensions or spacing");
    }

    std::vector<cv::KeyPoint> keypoints;
    createDarkCircleDetector(mono8.size())->detect(mono8, keypoints);
    const std::vector<OrderedGridPoint> grid_points =
        orderFixedViewDotGrid(keypoints, mono8.size(), grid_columns, grid_rows);
    if (grid_points.empty()) {
        return Status::Error(ErrorCode::FeatureNotFound, "fixed-view symmetric dot grid was not found");
    }

    const int point_count = static_cast<int>(grid_points.size());
    cv::Mat physical_design(point_count, 3, CV_64F);
    cv::Mat source_x(point_count, 1, CV_64F);
    cv::Mat source_y(point_count, 1, CV_64F);
    for (int index = 0; index < point_count; ++index) {
        physical_design.at<double>(index, 0) = grid_points[index].column * point_spacing_mm;
        physical_design.at<double>(index, 1) = grid_points[index].row * point_spacing_mm;
        physical_design.at<double>(index, 2) = 1.0;
        source_x.at<double>(index, 0) = grid_points[index].source_center.x;
        source_y.at<double>(index, 0) = grid_points[index].source_center.y;
    }

    cv::Mat affine_x;
    cv::Mat affine_y;
    if (!cv::solve(physical_design, source_x, affine_x, cv::DECOMP_SVD) ||
        !cv::solve(physical_design, source_y, affine_y, cv::DECOMP_SVD)) {
        return Status::Error(ErrorCode::FittingFailed, "ideal-grid affine fitting failed");
    }

    std::vector<cv::Point2f> target_centers;
    target_centers.reserve(point_count);
    cv::Mat polynomial_design(point_count, kDotGridPolynomialCoefficientCount, CV_64F);
    for (int index = 0; index < point_count; ++index) {
        const double physical_x = grid_points[index].column * point_spacing_mm;
        const double physical_y = grid_points[index].row * point_spacing_mm;
        const cv::Point2d target(
            affine_x.at<double>(0, 0) * physical_x + affine_x.at<double>(1, 0) * physical_y + affine_x.at<double>(2, 0),
            affine_y.at<double>(0, 0) * physical_x + affine_y.at<double>(1, 0) * physical_y + affine_y.at<double>(2, 0));
        target_centers.emplace_back(target);
        fillPolynomialTerms(target, mono8.size(), polynomial_design, index);
    }

    cv::Mat polynomial_x;
    cv::Mat polynomial_y;
    if (!cv::solve(polynomial_design, source_x, polynomial_x, cv::DECOMP_SVD) ||
        !cv::solve(polynomial_design, source_y, polynomial_y, cv::DECOMP_SVD)) {
        return Status::Error(ErrorCode::FittingFailed, "distortion polynomial fitting failed");
    }

    DotGridDistortionTemplate fitted_template;
    fitted_template.image_width = mono8.cols;
    fitted_template.image_height = mono8.rows;
    fitted_template.detected_point_count = point_count;
    for (int index = 0; index < kDotGridPolynomialCoefficientCount; ++index) {
        fitted_template.output_to_input_x[index] = polynomial_x.at<double>(index, 0);
        fitted_template.output_to_input_y[index] = polynomial_y.at<double>(index, 0);
    }
    if (!isFiniteTemplate(fitted_template)) {
        return Status::Error(ErrorCode::FittingFailed, "distortion polynomial contains non-finite coefficients");
    }

    double squared_error_sum = 0.0;
    for (int index = 0; index < point_count; ++index) {
        const cv::Point2d target(target_centers[index]);
        const cv::Point2d predicted_source(
            evaluatePolynomial(fitted_template.output_to_input_x, target, mono8.size()),
            evaluatePolynomial(fitted_template.output_to_input_y, target, mono8.size()));
        const cv::Point2d error = predicted_source - cv::Point2d(grid_points[index].source_center);
        squared_error_sum += error.dot(error);
    }
    fitted_template.rms_error_pixels = std::sqrt(squared_error_sum / point_count);

    cv::cvtColor(mono8, result_bgr, cv::COLOR_GRAY2BGR);
    for (int index = 0; index < point_count; ++index) {
        const cv::Point source = grid_points[index].source_center;
        const cv::Point target = target_centers[index];
        cv::arrowedLine(result_bgr, target, source, cv::Scalar(0, 0, 255), 1, cv::LINE_AA, 0, 0.2);
        cv::circle(result_bgr, source, 5, cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
        cv::circle(result_bgr, target, 4, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
        const int grid_index = grid_points[index].row * grid_columns + grid_points[index].column;
        cv::putText(result_bgr, std::to_string(grid_index), source + cv::Point(6, -6),
                    cv::FONT_HERSHEY_SIMPLEX, 0.35, cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
    }
    cv::putText(result_bgr, cv::format("RMS=%.4f px", fitted_template.rms_error_pixels), cv::Point(24, 36),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);

    output_template = fitted_template;
    return Status::OK();
}

Status DistortionCorrectionModule::correctByDotGridTemplate(
    const cv::Mat& mono8,
    const DotGridDistortionTemplate& distortion_template,
    cv::Mat& corrected_mono8
) {
    if (mono8.empty()) {
        return Status::Error(ErrorCode::ImageEmpty, "input image is empty");
    }
    if (mono8.type() != CV_8UC1) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "only CV_8UC1 is supported");
    }
    if (mono8.cols != distortion_template.image_width || mono8.rows != distortion_template.image_height) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "input image size does not match the distortion template");
    }
    if (!isFiniteTemplate(distortion_template)) {
        return Status::Error(ErrorCode::InvalidParam, "distortion template contains non-finite coefficients");
    }

    cv::Mat map_x(mono8.size(), CV_32FC1);
    cv::Mat map_y(mono8.size(), CV_32FC1);
    for (int row = 0; row < mono8.rows; ++row) {
        float* map_x_row = map_x.ptr<float>(row);
        float* map_y_row = map_y.ptr<float>(row);
        for (int column = 0; column < mono8.cols; ++column) {
            const cv::Point2d target(column, row);
            map_x_row[column] = static_cast<float>(
                evaluatePolynomial(distortion_template.output_to_input_x, target, mono8.size()));
            map_y_row[column] = static_cast<float>(
                evaluatePolynomial(distortion_template.output_to_input_y, target, mono8.size()));
        }
    }
    cv::remap(mono8, corrected_mono8, map_x, map_y, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
    return Status::OK();
}

} // namespace wafer_calib
