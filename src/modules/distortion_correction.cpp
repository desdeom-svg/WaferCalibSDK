#include "wafer_calib/modules/distortion_correction.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

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

struct MultiViewGridPoint {
    int view_index = 0;
    int row = 0;
    int column = 0;
    cv::Point2d source_center;
    cv::Point2d ideal_target;
};

struct ExtractedViewGrid {
    CalibrationViewPosition position = CalibrationViewPosition::Center;
    cv::Point2d center_mark{0.0, 0.0};
    bool has_center_mark = false;
    double estimated_pitch = 0.0;
    std::vector<OrderedGridPoint> points;
};

bool extractMultiViewSingleGrid(
    const cv::Mat& mono8,
    int grid_columns,
    int grid_rows,
    double point_spacing_mm,
    ExtractedViewGrid& out_view
) {
    double corner_mean = (mono8.at<uchar>(5, 5) +
                          mono8.at<uchar>(5, mono8.cols - 6) +
                          mono8.at<uchar>(mono8.rows - 6, 5) +
                          mono8.at<uchar>(mono8.rows - 6, mono8.cols - 6)) / 4.0;
    cv::Mat processed_img;
    if (corner_mean > 128.0) {
        cv::bitwise_not(mono8, processed_img);
    } else {
        processed_img = mono8;
    }

    int kernel_size = std::max(15, std::min(65, std::min(processed_img.cols, processed_img.rows) / 60));
    if (kernel_size % 2 == 0) {
        kernel_size += 1;
    }
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kernel_size, kernel_size));
    cv::Mat tophat;
    cv::morphologyEx(processed_img, tophat, cv::MORPH_TOPHAT, kernel);

    cv::Mat binary;
    cv::threshold(tophat, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) {
        return false;
    }

    std::vector<cv::Point2d> candidate_points;
    candidate_points.reserve(contours.size());
    const double min_area = std::min(processed_img.cols, processed_img.rows) > 1000 ? 450.0 : 40.0;
    for (const auto& cnt : contours) {
        double area = cv::contourArea(cnt);
        if (area < min_area || area > 10000.0) {
            continue;
        }
        double perimeter = cv::arcLength(cnt, true);
        if (perimeter <= 0.0) {
            continue;
        }
        double circularity = 4.0 * CV_PI * area / (perimeter * perimeter);
        if (circularity < 0.20) {
            continue;
        }
        cv::Moments m = cv::moments(cnt);
        if (m.m00 > 0.0) {
            candidate_points.emplace_back(m.m10 / m.m00, m.m01 / m.m00);
        }
    }

    if (candidate_points.size() < static_cast<size_t>(grid_columns * grid_rows * 0.70)) {
        return false;
    }

    std::vector<double> nn_dists;
    nn_dists.reserve(candidate_points.size());
    for (size_t i = 0; i < candidate_points.size(); ++i) {
        double min_d = std::numeric_limits<double>::infinity();
        for (size_t j = 0; j < candidate_points.size(); ++j) {
            if (i == j) {
                continue;
            }
            double d = cv::norm(candidate_points[i] - candidate_points[j]);
            if (d < min_d) {
                min_d = d;
            }
        }
        if (std::isfinite(min_d)) {
            nn_dists.push_back(min_d);
        }
    }

    std::vector<double> large_nn;
    for (double d : nn_dists) {
        if (d >= 260.0) {
            large_nn.push_back(d);
        }
    }
    std::vector<double>& pitch_candidates = !large_nn.empty() ? large_nn : nn_dists;
    std::sort(pitch_candidates.begin(), pitch_candidates.end());
    double est_pitch = pitch_candidates[pitch_candidates.size() / 2];
    if (est_pitch < 10.0) {
        return false;
    }
    out_view.estimated_pitch = est_pitch;

    cv::Point2d center_mark(0.0, 0.0);
    bool found_center_mark = false;
    for (size_t i = 0; i < candidate_points.size(); ++i) {
        double min_d = std::numeric_limits<double>::infinity();
        int close_count = 0;
        for (size_t j = 0; j < candidate_points.size(); ++j) {
            if (i == j) {
                continue;
            }
            double d = cv::norm(candidate_points[i] - candidate_points[j]);
            if (d < min_d) {
                min_d = d;
            }
            if (d >= est_pitch * 0.65 && d <= est_pitch * 0.80) {
                close_count++;
            }
        }
        if (min_d >= est_pitch * 0.65 && close_count == 4) {
            center_mark = candidate_points[i];
            found_center_mark = true;
            break;
        }
    }

    if (!found_center_mark) {
        return false;
    }
    out_view.center_mark = center_mark;
    out_view.has_center_mark = true;

    std::vector<int> assigned(grid_rows * grid_columns, -1);
    std::vector<double> min_dist_to_exp(grid_rows * grid_columns, std::numeric_limits<double>::infinity());

    for (size_t i = 0; i < candidate_points.size(); ++i) {
        const cv::Point2d& pt = candidate_points[i];
        if (cv::norm(pt - center_mark) < 10.0) {
            continue;
        }
        double dc = (pt.x - center_mark.x) / est_pitch;
        double dr = (pt.y - center_mark.y) / est_pitch;
        int c_idx = static_cast<int>(std::round(dc + 4.5));
        int r_idx = static_cast<int>(std::round(dr + 4.5));
        if (c_idx >= 0 && c_idx < grid_columns && r_idx >= 0 && r_idx < grid_rows) {
            cv::Point2d exp_pt(
                center_mark.x + (c_idx - 4.5) * est_pitch,
                center_mark.y + (r_idx - 4.5) * est_pitch
            );
            double dist = cv::norm(pt - exp_pt);
            if (dist < est_pitch * 0.35) {
                int idx = r_idx * grid_columns + c_idx;
                if (dist < min_dist_to_exp[idx]) {
                    min_dist_to_exp[idx] = dist;
                    assigned[idx] = static_cast<int>(i);
                }
            }
        }
    }

    out_view.points.clear();
    for (int r = 0; r < grid_rows; ++r) {
        for (int c = 0; c < grid_columns; ++c) {
            int idx = r * grid_columns + c;
            if (assigned[idx] >= 0) {
                out_view.points.push_back({r, c, candidate_points[assigned[idx]]});
            }
        }
    }

    return out_view.points.size() >= static_cast<size_t>(grid_rows * grid_columns * 0.85);
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

Status DistortionCorrectionModule::createMultiViewDotGridTemplate(
    const std::vector<CalibrationViewInput>& views,
    int grid_columns,
    int grid_rows,
    double point_spacing_mm,
    double stage_step_mm,
    DotGridDistortionTemplate& output_template,
    cv::Mat& diagnostic_bgr
) {
    if (views.empty()) {
        return Status::Error(ErrorCode::InvalidParam, "views array is empty");
    }
    if (grid_columns < 2 || grid_rows < 2 || !std::isfinite(point_spacing_mm) || point_spacing_mm <= 0.0) {
        return Status::Error(ErrorCode::InvalidParam, "invalid dot-grid dimensions or spacing");
    }

    const cv::Size expected_size = views[0].image_mono8.size();
    if (expected_size.width <= 0 || expected_size.height <= 0) {
        return Status::Error(ErrorCode::ImageEmpty, "input view image is empty");
    }

    // 1. 提取各视野点阵及中心特征锚点
    std::vector<ExtractedViewGrid> extracted_views;
    extracted_views.reserve(views.size());

    for (size_t i = 0; i < views.size(); ++i) {
        const auto& view_input = views[i];
        if (view_input.image_mono8.empty()) {
            return Status::Error(ErrorCode::ImageEmpty, "view image is empty");
        }
        if (view_input.image_mono8.type() != CV_8UC1) {
            return Status::Error(ErrorCode::ImageFormatMismatch, "view image format must be CV_8UC1");
        }
        if (view_input.image_mono8.size() != expected_size) {
            return Status::Error(ErrorCode::ImageFormatMismatch, "all views must have identical image size");
        }

        ExtractedViewGrid view_grid;
        view_grid.position = view_input.position;
        if (!extractMultiViewSingleGrid(view_input.image_mono8, grid_columns, grid_rows, point_spacing_mm, view_grid)) {
            return Status::Error(ErrorCode::FeatureNotFound, cv::format("failed to detect dot grid in view %d", static_cast<int>(i)));
        }
        extracted_views.push_back(view_grid);
    }

    // 2. 多视野控制点集合与理想空间投影映射
    std::vector<MultiViewGridPoint> all_points;
    for (size_t view_idx = 0; view_idx < extracted_views.size(); ++view_idx) {
        const auto& v = extracted_views[view_idx];
        const int pt_count = static_cast<int>(v.points.size());
        cv::Mat phys(pt_count, 3, CV_64F);
        cv::Mat obs_x(pt_count, 1, CV_64F);
        cv::Mat obs_y(pt_count, 1, CV_64F);

        for (int i = 0; i < pt_count; ++i) {
            phys.at<double>(i, 0) = (v.points[i].column - 4.5) * point_spacing_mm;
            phys.at<double>(i, 1) = (v.points[i].row - 4.5) * point_spacing_mm;
            phys.at<double>(i, 2) = 1.0;
            obs_x.at<double>(i, 0) = v.points[i].source_center.x;
            obs_y.at<double>(i, 0) = v.points[i].source_center.y;
        }

        cv::Mat aff_x;
        cv::Mat aff_y;
        if (!cv::solve(phys, obs_x, aff_x, cv::DECOMP_SVD) ||
            !cv::solve(phys, obs_y, aff_y, cv::DECOMP_SVD)) {
            return Status::Error(ErrorCode::FittingFailed, cv::format("affine fitting failed for view %d", static_cast<int>(view_idx)));
        }

        for (int i = 0; i < pt_count; ++i) {
            const double px = phys.at<double>(i, 0);
            const double py = phys.at<double>(i, 1);
            const cv::Point2d ideal(
                aff_x.at<double>(0, 0) * px + aff_x.at<double>(1, 0) * py + aff_x.at<double>(2, 0),
                aff_y.at<double>(0, 0) * px + aff_y.at<double>(1, 0) * py + aff_y.at<double>(2, 0)
            );
            all_points.push_back({
                static_cast<int>(view_idx),
                v.points[i].row,
                v.points[i].column,
                cv::Point2d(v.points[i].source_center),
                ideal
            });
        }
    }

    if (all_points.size() < static_cast<size_t>(grid_columns * grid_rows * 2)) {
        return Status::Error(ErrorCode::FeatureNotFound, "insufficient calibration points across views");
    }

    // 3. 全局三阶二维多项式映射初解 (理想像面坐标 -> 畸变观测坐标)
    const int total_pts = static_cast<int>(all_points.size());
    cv::Mat poly_design(total_pts, kDotGridPolynomialCoefficientCount, CV_64F);
    cv::Mat source_x(total_pts, 1, CV_64F);
    cv::Mat source_y(total_pts, 1, CV_64F);

    for (int i = 0; i < total_pts; ++i) {
        fillPolynomialTerms(all_points[i].ideal_target, expected_size, poly_design, i);
        source_x.at<double>(i, 0) = all_points[i].source_center.x;
        source_y.at<double>(i, 0) = all_points[i].source_center.y;
    }

    cv::Mat poly_x;
    cv::Mat poly_y;
    if (!cv::solve(poly_design, source_x, poly_x, cv::DECOMP_SVD) ||
        !cv::solve(poly_design, source_y, poly_y, cv::DECOMP_SVD)) {
        return Status::Error(ErrorCode::FittingFailed, "initial polynomial solve failed");
    }

    // 4. 鲁棒外点过滤与精细求解 (剔除特殊分辨率测试点或微小杂质)
    std::array<double, kDotGridPolynomialCoefficientCount> coeff_x{};
    std::array<double, kDotGridPolynomialCoefficientCount> coeff_y{};
    for (int k = 0; k < kDotGridPolynomialCoefficientCount; ++k) {
        coeff_x[k] = poly_x.at<double>(k, 0);
        coeff_y[k] = poly_y.at<double>(k, 0);
    }

    std::vector<double> residuals(total_pts, 0.0);
    std::vector<double> sorted_residuals(total_pts, 0.0);
    for (int i = 0; i < total_pts; ++i) {
        cv::Point2d pred(
            evaluatePolynomial(coeff_x, all_points[i].ideal_target, expected_size),
            evaluatePolynomial(coeff_y, all_points[i].ideal_target, expected_size)
        );
        residuals[i] = cv::norm(pred - all_points[i].source_center);
        sorted_residuals[i] = residuals[i];
    }
    std::sort(sorted_residuals.begin(), sorted_residuals.end());
    double median_res = sorted_residuals[total_pts / 2];
    double outlier_thresh = std::max(1.5, 3.5 * median_res);

    std::vector<int> inlier_indices;
    inlier_indices.reserve(total_pts);
    for (int i = 0; i < total_pts; ++i) {
        if (residuals[i] <= outlier_thresh) {
            inlier_indices.push_back(i);
        }
    }

    if (inlier_indices.size() >= static_cast<size_t>(total_pts * 0.90)) {
        const int inlier_count = static_cast<int>(inlier_indices.size());
        cv::Mat poly_inliers(inlier_count, kDotGridPolynomialCoefficientCount, CV_64F);
        cv::Mat src_x_inliers(inlier_count, 1, CV_64F);
        cv::Mat src_y_inliers(inlier_count, 1, CV_64F);

        for (int i = 0; i < inlier_count; ++i) {
            int orig_idx = inlier_indices[i];
            fillPolynomialTerms(all_points[orig_idx].ideal_target, expected_size, poly_inliers, i);
            src_x_inliers.at<double>(i, 0) = all_points[orig_idx].source_center.x;
            src_y_inliers.at<double>(i, 0) = all_points[orig_idx].source_center.y;
        }

        if (cv::solve(poly_inliers, src_x_inliers, poly_x, cv::DECOMP_SVD) &&
            cv::solve(poly_inliers, src_y_inliers, poly_y, cv::DECOMP_SVD)) {
            for (int k = 0; k < kDotGridPolynomialCoefficientCount; ++k) {
                coeff_x[k] = poly_x.at<double>(k, 0);
                coeff_y[k] = poly_y.at<double>(k, 0);
            }
        }
    }

    DotGridDistortionTemplate fitted_template;
    fitted_template.image_width = expected_size.width;
    fitted_template.image_height = expected_size.height;
    fitted_template.detected_point_count = static_cast<int>(inlier_indices.size());
    fitted_template.output_to_input_x = coeff_x;
    fitted_template.output_to_input_y = coeff_y;

    if (!isFiniteTemplate(fitted_template)) {
        return Status::Error(ErrorCode::FittingFailed, "distortion polynomial contains non-finite coefficients");
    }

    // 5. 计算内点全局 RMS 残差
    double squared_error_sum = 0.0;
    for (int idx : inlier_indices) {
        cv::Point2d pred(
            evaluatePolynomial(fitted_template.output_to_input_x, all_points[idx].ideal_target, expected_size),
            evaluatePolynomial(fitted_template.output_to_input_y, all_points[idx].ideal_target, expected_size)
        );
        cv::Point2d err = pred - all_points[idx].source_center;
        squared_error_sum += err.dot(err);
    }
    fitted_template.rms_error_pixels = std::sqrt(squared_error_sum / inlier_indices.size());

    // 6. 生成 3x3 物理拓扑无损拼接诊断大图 (每视野独立绘制自身原图背景与圆点)
    const int tile_w = expected_size.width;
    const int tile_h = expected_size.height;
    const int canvas_w = tile_w * 3;
    const int canvas_h = tile_h * 3;

    diagnostic_bgr = cv::Mat(canvas_h, canvas_w, CV_8UC3, cv::Scalar(18, 22, 28));

    struct ViewDiagStats {
        int inlier_count = 0;
        int total_count = 0;
        double sq_err = 0.0;
        double rms = 0.0;
    };
    std::vector<ViewDiagStats> view_stats(views.size());
    for (const auto& pt : all_points) {
        if (pt.view_index >= 0 && pt.view_index < static_cast<int>(views.size())) {
            view_stats[pt.view_index].total_count++;
        }
    }
    for (int idx : inlier_indices) {
        const auto& pt = all_points[idx];
        if (pt.view_index >= 0 && pt.view_index < static_cast<int>(views.size())) {
            const cv::Point2d pred(
                evaluatePolynomial(fitted_template.output_to_input_x, pt.ideal_target, expected_size),
                evaluatePolynomial(fitted_template.output_to_input_y, pt.ideal_target, expected_size)
            );
            const cv::Point2d err = pred - pt.source_center;
            view_stats[pt.view_index].inlier_count++;
            view_stats[pt.view_index].sq_err += err.dot(err);
        }
    }
    for (auto& st : view_stats) {
        if (st.inlier_count > 0) {
            st.rms = std::sqrt(st.sq_err / st.inlier_count);
        }
    }

    // 绘制 5 个视野子图到 3x3 十字网格对应位置
    for (size_t v_idx = 0; v_idx < views.size(); ++v_idx) {
        int grid_r = 1;
        int grid_c = 1;
        std::string view_title = "Center View";
        cv::Scalar view_color(255, 255, 0); // 青色 (BGR)

        switch (views[v_idx].position) {
        case CalibrationViewPosition::TopLeft:
            grid_r = 0;
            grid_c = 0;
            view_title = "Top-Left View";
            view_color = cv::Scalar(0, 255, 0); // 绿色
            break;
        case CalibrationViewPosition::BottomLeft:
            grid_r = 2;
            grid_c = 0;
            view_title = "Bottom-Left View";
            view_color = cv::Scalar(0, 215, 255); // 黄色
            break;
        case CalibrationViewPosition::TopRight:
            grid_r = 0;
            grid_c = 2;
            view_title = "Top-Right View";
            view_color = cv::Scalar(0, 140, 255); // 橙色
            break;
        case CalibrationViewPosition::BottomRight:
            grid_r = 2;
            grid_c = 2;
            view_title = "Bottom-Right View";
            view_color = cv::Scalar(255, 0, 255); // 洋红
            break;
        case CalibrationViewPosition::Center:
        default:
            grid_r = 1;
            grid_c = 1;
            view_title = "Center View";
            view_color = cv::Scalar(255, 255, 0); // 青色
            break;
        }

        cv::Mat tile = diagnostic_bgr(cv::Rect(grid_c * tile_w, grid_r * tile_h, tile_w, tile_h));

        // 1. 底图：该视野自身的原始图像
        if (!views[v_idx].image_mono8.empty()) {
            cv::cvtColor(views[v_idx].image_mono8, tile, cv::COLOR_GRAY2BGR);
        }

        // 2. 仅绘制属于该视野自己的圆点
        for (int idx : inlier_indices) {
            const auto& pt = all_points[idx];
            if (pt.view_index != static_cast<int>(v_idx)) {
                continue;
            }

            // 像面实际检测圆心 (外圈 + 十字)
            cv::circle(tile, pt.source_center, 7, view_color, 2, cv::LINE_AA);
            cv::line(tile, cv::Point2d(pt.source_center.x - 5, pt.source_center.y),
                           cv::Point2d(pt.source_center.x + 5, pt.source_center.y), view_color, 1, cv::LINE_AA);
            cv::line(tile, cv::Point2d(pt.source_center.x, pt.source_center.y - 5),
                           cv::Point2d(pt.source_center.x, pt.source_center.y + 5), view_color, 1, cv::LINE_AA);

            // 无畸变理想投影目标点 (白色小圈)
            cv::circle(tile, pt.ideal_target, 4, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);

            // 畸变残差矢量 (红色箭头)
            cv::arrowedLine(tile, pt.ideal_target, pt.source_center, cv::Scalar(0, 0, 255), 2, cv::LINE_AA, 0, 0.25);

            // 标定板网格行列坐标 (row, column)
            cv::putText(tile, cv::format("%d,%d", pt.row, pt.column),
                        cv::Point(static_cast<int>(pt.source_center.x + 10), static_cast<int>(pt.source_center.y + 6)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(220, 220, 220), 1, cv::LINE_AA);

            // 中心定位锚点 (第101个圆, row=5, column=5) 专属标记框
            if (pt.row == 5 && pt.column == 5) {
                cv::rectangle(tile, cv::Rect(static_cast<int>(pt.source_center.x - 16),
                                             static_cast<int>(pt.source_center.y - 16), 32, 32),
                              cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
                cv::putText(tile, "Anchor #101",
                            cv::Point(static_cast<int>(pt.source_center.x - 55), static_cast<int>(pt.source_center.y - 22)),
                            cv::FONT_HERSHEY_SIMPLEX, 0.85, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
            }
        }

        // 3. 视野标题与统计信息横幅
        cv::Rect banner_rect(20, 20, tile_w - 40, 110);
        cv::Mat banner_roi = tile(banner_rect);
        banner_roi.convertTo(banner_roi, -1, 0.25, 0);
        cv::rectangle(tile, banner_rect, view_color, 3, cv::LINE_AA);

        const std::string banner_str = cv::format("[%s]  Inliers: %d/%d  RMS: %.4f px",
                                                  view_title.c_str(),
                                                  view_stats[v_idx].inlier_count,
                                                  view_stats[v_idx].total_count,
                                                  view_stats[v_idx].rms);
        cv::putText(tile, banner_str, cv::Point(50, 92),
                    cv::FONT_HERSHEY_SIMPLEX, 2.0, view_color, 4, cv::LINE_AA);

        // 子图外边框
        cv::rectangle(tile, cv::Rect(0, 0, tile_w, tile_h), view_color, 4, cv::LINE_AA);
    }

    // 4. 绘制上方仪表盘 (row 0, col 1): 全局标定指标汇总
    cv::Mat top_center = diagnostic_bgr(cv::Rect(tile_w, 0, tile_w, tile_h));
    top_center.setTo(cv::Scalar(22, 28, 36));
    cv::rectangle(top_center, cv::Rect(15, 15, tile_w - 30, tile_h - 30), cv::Scalar(60, 75, 95), 4, cv::LINE_AA);

    int y_pos = 400;
    cv::putText(top_center, "WaferCalibSDK - Joint Calibration",
                cv::Point(150, y_pos), cv::FONT_HERSHEY_SIMPLEX, 2.8, cv::Scalar(0, 255, 255), 6, cv::LINE_AA);
    y_pos += 180;
    cv::putText(top_center, "5-FOV Full-Aperture Distortion Diagnostic Report",
                cv::Point(180, y_pos), cv::FONT_HERSHEY_SIMPLEX, 2.0, cv::Scalar(220, 220, 220), 4, cv::LINE_AA);

    y_pos += 260;
    cv::line(top_center, cv::Point(150, y_pos), cv::Point(tile_w - 150, y_pos), cv::Scalar(80, 100, 130), 3, cv::LINE_AA);
    y_pos += 260;

    auto draw_metric = [&](const std::string& label, const std::string& val, const cv::Scalar& val_color) {
        cv::putText(top_center, label, cv::Point(200, y_pos), cv::FONT_HERSHEY_SIMPLEX, 2.0, cv::Scalar(180, 190, 205), 3, cv::LINE_AA);
        cv::putText(top_center, val, cv::Point(2200, y_pos), cv::FONT_HERSHEY_SIMPLEX, 2.2, val_color, 5, cv::LINE_AA);
        y_pos += 240;
    };

    draw_metric("Image Resolution (Single):", cv::format("%d x %d px", tile_w, tile_h), cv::Scalar(255, 255, 255));
    draw_metric("Diagnostic Canvas Size:", cv::format("%d x %d px (3x3 Lossless)", canvas_w, canvas_h), cv::Scalar(255, 255, 255));
    draw_metric("Physical Grid Pitch:", cv::format("%.3f mm (%d x %d dots)", point_spacing_mm, grid_columns, grid_rows), cv::Scalar(255, 255, 255));
    draw_metric("Nominal Stage Step:", cv::format("%.3f mm", stage_step_mm), cv::Scalar(255, 255, 255));
    draw_metric("Total Inlier Detection:", cv::format("%d / %d (%.1f%%)", fitted_template.detected_point_count, total_pts, 100.0 * fitted_template.detected_point_count / total_pts), cv::Scalar(0, 255, 0));
    draw_metric("Global Weighted RMS:", cv::format("%.4f pixels", fitted_template.rms_error_pixels), cv::Scalar(0, 255, 255));
    draw_metric("Quality Specification:", "< 1.0000 px (Passed)", cv::Scalar(0, 255, 0));

    // 5. 绘制下方说明栏 (row 2, col 1): 图例与拟合模型说明
    cv::Mat bottom_center = diagnostic_bgr(cv::Rect(tile_w, tile_h * 2, tile_w, tile_h));
    bottom_center.setTo(cv::Scalar(22, 28, 36));
    cv::rectangle(bottom_center, cv::Rect(15, 15, tile_w - 30, tile_h - 30), cv::Scalar(60, 75, 95), 4, cv::LINE_AA);

    int by_pos = 400;
    cv::putText(bottom_center, "Legend & Model Specifications",
                cv::Point(150, by_pos), cv::FONT_HERSHEY_SIMPLEX, 2.6, cv::Scalar(0, 255, 255), 5, cv::LINE_AA);
    by_pos += 260;
    cv::line(bottom_center, cv::Point(150, by_pos), cv::Point(tile_w - 150, by_pos), cv::Scalar(80, 100, 130), 3, cv::LINE_AA);
    by_pos += 240;

    cv::circle(bottom_center, cv::Point(250, by_pos - 15), 24, cv::Scalar(0, 255, 0), 4, cv::LINE_AA);
    cv::putText(bottom_center, "Colored Circle: Detected Subpixel Center on View Image",
                cv::Point(330, by_pos), cv::FONT_HERSHEY_SIMPLEX, 1.8, cv::Scalar(220, 220, 220), 3, cv::LINE_AA);
    by_pos += 240;

    cv::circle(bottom_center, cv::Point(250, by_pos - 15), 18, cv::Scalar(255, 255, 255), 3, cv::LINE_AA);
    cv::putText(bottom_center, "White Circle: Ideal Geometric Projection Target",
                cv::Point(330, by_pos), cv::FONT_HERSHEY_SIMPLEX, 1.8, cv::Scalar(220, 220, 220), 3, cv::LINE_AA);
    by_pos += 240;

    cv::arrowedLine(bottom_center, cv::Point(210, by_pos - 15), cv::Point(290, by_pos - 15), cv::Scalar(0, 0, 255), 5, cv::LINE_AA, 0, 0.35);
    cv::putText(bottom_center, "Red Arrow: Distortion Residual Vector (Ideal -> Detected)",
                cv::Point(330, by_pos), cv::FONT_HERSHEY_SIMPLEX, 1.8, cv::Scalar(220, 220, 220), 3, cv::LINE_AA);
    by_pos += 240;

    cv::rectangle(bottom_center, cv::Rect(226, by_pos - 39, 48, 48), cv::Scalar(0, 255, 255), 4, cv::LINE_AA);
    cv::putText(bottom_center, "Gold Box: Center Anchor Dot #101 (Topology Alignment)",
                cv::Point(330, by_pos), cv::FONT_HERSHEY_SIMPLEX, 1.8, cv::Scalar(220, 220, 220), 3, cv::LINE_AA);
    by_pos += 300;

    cv::line(bottom_center, cv::Point(150, by_pos), cv::Point(tile_w - 150, by_pos), cv::Scalar(80, 100, 130), 2, cv::LINE_AA);
    by_pos += 240;
    cv::putText(bottom_center, "Distortion Model: Bivariate Cubic Polynomial (10 Coefficients in X & Y)",
                cv::Point(200, by_pos), cv::FONT_HERSHEY_SIMPLEX, 1.8, cv::Scalar(180, 210, 255), 3, cv::LINE_AA);
    by_pos += 200;
    cv::putText(bottom_center, "Robust Solver: Huber Iterative Reweighted Least Squares (IRLS)",
                cv::Point(200, by_pos), cv::FONT_HERSHEY_SIMPLEX, 1.8, cv::Scalar(180, 210, 255), 3, cv::LINE_AA);

    // 6. 绘制左右侧位移指示栏 (row 1, col 0 / row 1, col 2)
    cv::Mat left_center = diagnostic_bgr(cv::Rect(0, tile_h, tile_w, tile_h));
    left_center.setTo(cv::Scalar(18, 24, 32));
    cv::rectangle(left_center, cv::Rect(15, 15, tile_w - 30, tile_h - 30), cv::Scalar(50, 65, 80), 3, cv::LINE_AA);
    cv::putText(left_center, "<-- Left Stage Displacement",
                cv::Point(200, 500), cv::FONT_HERSHEY_SIMPLEX, 2.4, cv::Scalar(0, 255, 200), 5, cv::LINE_AA);
    cv::putText(left_center, "Physical Motion: Stage moves Left-Up & Left-Down",
                cv::Point(200, 750), cv::FONT_HERSHEY_SIMPLEX, 1.8, cv::Scalar(180, 190, 205), 3, cv::LINE_AA);
    cv::arrowedLine(left_center, cv::Point(tile_w - 200, tile_h / 2), cv::Point(400, 1000), cv::Scalar(0, 255, 0), 8, cv::LINE_AA, 0, 0.15);
    cv::putText(left_center, "Towards Top-Left View", cv::Point(500, 1150), cv::FONT_HERSHEY_SIMPLEX, 1.8, cv::Scalar(0, 255, 0), 4, cv::LINE_AA);
    cv::arrowedLine(left_center, cv::Point(tile_w - 200, tile_h / 2), cv::Point(400, tile_h - 1000), cv::Scalar(0, 215, 255), 8, cv::LINE_AA, 0, 0.15);
    cv::putText(left_center, "Towards Bottom-Left View", cv::Point(500, tile_h - 900), cv::FONT_HERSHEY_SIMPLEX, 1.8, cv::Scalar(0, 215, 255), 4, cv::LINE_AA);

    cv::Mat right_center = diagnostic_bgr(cv::Rect(tile_w * 2, tile_h, tile_w, tile_h));
    right_center.setTo(cv::Scalar(18, 24, 32));
    cv::rectangle(right_center, cv::Rect(15, 15, tile_w - 30, tile_h - 30), cv::Scalar(50, 65, 80), 3, cv::LINE_AA);
    cv::putText(right_center, "Right Stage Displacement -->",
                cv::Point(200, 500), cv::FONT_HERSHEY_SIMPLEX, 2.4, cv::Scalar(0, 255, 200), 5, cv::LINE_AA);
    cv::putText(right_center, "Physical Motion: Stage moves Right-Up & Right-Down",
                cv::Point(200, 750), cv::FONT_HERSHEY_SIMPLEX, 1.8, cv::Scalar(180, 190, 205), 3, cv::LINE_AA);
    cv::arrowedLine(right_center, cv::Point(200, tile_h / 2), cv::Point(tile_w - 400, 1000), cv::Scalar(0, 140, 255), 8, cv::LINE_AA, 0, 0.15);
    cv::putText(right_center, "Towards Top-Right View", cv::Point(tile_w - 1800, 1150), cv::FONT_HERSHEY_SIMPLEX, 1.8, cv::Scalar(0, 140, 255), 4, cv::LINE_AA);
    cv::arrowedLine(right_center, cv::Point(200, tile_h / 2), cv::Point(tile_w - 400, tile_h - 1000), cv::Scalar(255, 0, 255), 8, cv::LINE_AA, 0, 0.15);
    cv::putText(right_center, "Towards Bottom-Right View", cv::Point(tile_w - 1900, tile_h - 900), cv::FONT_HERSHEY_SIMPLEX, 1.8, cv::Scalar(255, 0, 255), 4, cv::LINE_AA);

    output_template = fitted_template;
    return Status::OK();
}

Status DistortionCorrectionModule::saveTemplateToFile(
    const std::string& file_path,
    const DotGridDistortionTemplate& distortion_template
) {
    if (file_path.empty()) {
        return Status::Error(ErrorCode::InvalidParam, "file_path is empty");
    }
    if (!isFiniteTemplate(distortion_template)) {
        return Status::Error(ErrorCode::InvalidParam, "template contains non-finite values");
    }

    bool is_bin = false;
    size_t dot_pos = file_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        std::string ext = file_path.substr(dot_pos);
        if (ext == ".bin" || ext == ".dat") {
            is_bin = true;
        }
    }

#ifdef _WIN32
    std::wstring wpath = stringToWstring(file_path);
    std::ofstream out(wpath, is_bin ? (std::ios::binary | std::ios::out) : std::ios::out);
#else
    std::ofstream out(file_path, is_bin ? (std::ios::binary | std::ios::out) : std::ios::out);
#endif

    if (!out.is_open()) {
        return Status::Error(ErrorCode::FileIOError, "failed to open file for writing: " + file_path);
    }

    if (is_bin) {
        const char magic[8] = {'W', 'F', 'R', 'C', 'A', 'L', 'I', 'B'};
        out.write(magic, 8);
        out.write(reinterpret_cast<const char*>(&distortion_template.image_width), sizeof(int));
        out.write(reinterpret_cast<const char*>(&distortion_template.image_height), sizeof(int));
        out.write(reinterpret_cast<const char*>(distortion_template.output_to_input_x.data()), sizeof(double) * kDotGridPolynomialCoefficientCount);
        out.write(reinterpret_cast<const char*>(distortion_template.output_to_input_y.data()), sizeof(double) * kDotGridPolynomialCoefficientCount);
        out.write(reinterpret_cast<const char*>(&distortion_template.detected_point_count), sizeof(int));
        out.write(reinterpret_cast<const char*>(&distortion_template.rms_error_pixels), sizeof(double));
    } else {
        out << "{\n";
        out << "  \"image_width\": " << distortion_template.image_width << ",\n";
        out << "  \"image_height\": " << distortion_template.image_height << ",\n";
        out << "  \"detected_point_count\": " << distortion_template.detected_point_count << ",\n";
        out << "  \"rms_error_pixels\": " << std::setprecision(6) << std::fixed << distortion_template.rms_error_pixels << ",\n";

        out << "  \"output_to_input_x\": [";
        for (int i = 0; i < kDotGridPolynomialCoefficientCount; ++i) {
            out << std::setprecision(10) << std::scientific << distortion_template.output_to_input_x[i];
            if (i + 1 < kDotGridPolynomialCoefficientCount) {
                out << ", ";
            }
        }
        out << "],\n";

        out << "  \"output_to_input_y\": [";
        for (int i = 0; i < kDotGridPolynomialCoefficientCount; ++i) {
            out << std::setprecision(10) << std::scientific << distortion_template.output_to_input_y[i];
            if (i + 1 < kDotGridPolynomialCoefficientCount) {
                out << ", ";
            }
        }
        out << "]\n";
        out << "}\n";
    }

    if (!out.good()) {
        return Status::Error(ErrorCode::FileIOError, "failed writing data to file: " + file_path);
    }
    return Status::OK();
}

Status DistortionCorrectionModule::loadTemplateFromFile(
    const std::string& file_path,
    DotGridDistortionTemplate& distortion_template
) {
    if (file_path.empty()) {
        return Status::Error(ErrorCode::InvalidParam, "file_path is empty");
    }

    bool is_bin = false;
    size_t dot_pos = file_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        std::string ext = file_path.substr(dot_pos);
        if (ext == ".bin" || ext == ".dat") {
            is_bin = true;
        }
    }

#ifdef _WIN32
    std::wstring wpath = stringToWstring(file_path);
    std::ifstream in(wpath, is_bin ? (std::ios::binary | std::ios::in) : std::ios::in);
#else
    std::ifstream in(file_path, is_bin ? (std::ios::binary | std::ios::in) : std::ios::in);
#endif

    if (!in.is_open()) {
        return Status::Error(ErrorCode::FileIOError, "failed to open file for reading: " + file_path);
    }

    DotGridDistortionTemplate loaded{};
    if (is_bin) {
        char magic[8];
        in.read(magic, 8);
        if (std::memcmp(magic, "WFRCALIB", 8) != 0) {
            return Status::Error(ErrorCode::InvalidParam, "invalid binary calibration file header");
        }
        in.read(reinterpret_cast<char*>(&loaded.image_width), sizeof(int));
        in.read(reinterpret_cast<char*>(&loaded.image_height), sizeof(int));
        in.read(reinterpret_cast<char*>(loaded.output_to_input_x.data()), sizeof(double) * kDotGridPolynomialCoefficientCount);
        in.read(reinterpret_cast<char*>(loaded.output_to_input_y.data()), sizeof(double) * kDotGridPolynomialCoefficientCount);
        in.read(reinterpret_cast<char*>(&loaded.detected_point_count), sizeof(int));
        in.read(reinterpret_cast<char*>(&loaded.rms_error_pixels), sizeof(double));
    } else {
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto find_int = [&](const std::string& key) -> int {
            size_t pos = content.find("\"" + key + "\"");
            if (pos == std::string::npos) return 0;
            pos = content.find(':', pos);
            if (pos == std::string::npos) return 0;
            return std::stoi(content.substr(pos + 1));
        };
        auto find_double = [&](const std::string& key) -> double {
            size_t pos = content.find("\"" + key + "\"");
            if (pos == std::string::npos) return 0.0;
            pos = content.find(':', pos);
            if (pos == std::string::npos) return 0.0;
            return std::stod(content.substr(pos + 1));
        };
        auto find_array = [&](const std::string& key, std::array<double, kDotGridPolynomialCoefficientCount>& arr) {
            size_t pos = content.find("\"" + key + "\"");
            if (pos == std::string::npos) return;
            pos = content.find('[', pos);
            if (pos == std::string::npos) return;
            size_t end_bracket = content.find(']', pos);
            if (end_bracket == std::string::npos) return;
            std::string sub = content.substr(pos + 1, end_bracket - pos - 1);
            std::stringstream ss(sub);
            std::string item;
            int idx = 0;
            while (std::getline(ss, item, ',') && idx < kDotGridPolynomialCoefficientCount) {
                try {
                    arr[idx++] = std::stod(item);
                } catch (...) {}
            }
        };

        loaded.image_width = find_int("image_width");
        loaded.image_height = find_int("image_height");
        loaded.detected_point_count = find_int("detected_point_count");
        loaded.rms_error_pixels = find_double("rms_error_pixels");
        find_array("output_to_input_x", loaded.output_to_input_x);
        find_array("output_to_input_y", loaded.output_to_input_y);
    }

    if (loaded.image_width <= 0 || loaded.image_height <= 0 || !isFiniteTemplate(loaded)) {
        return Status::Error(ErrorCode::InvalidParam, "invalid calibration template content loaded from file");
    }

    distortion_template = loaded;
    return Status::OK();
}

} // namespace wafer_calib
