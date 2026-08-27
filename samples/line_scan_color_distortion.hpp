#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/imgproc.hpp>

namespace line_scan_color_distortion {

struct CalibrationResult {
    std::vector<cv::Point2f> centers;
    std::vector<cv::Point2i> lattice_indices;
    cv::Vec3d source_x_coefficients{};
    cv::Vec3d source_y_coefficients{};
    cv::Point2d output_origin{};
    double output_pitch_pixels = 0.0;
    double affine_x_rms_pixels = 0.0;
    double fitted_x_rms_pixels = 0.0;
    double y_affine_rms_pixels = 0.0;
    double pixels_per_mm_x = 0.0;
    double pixels_per_mm_y = 0.0;
};

namespace detail {

struct Candidate {
    cv::Point2f center;
    cv::Rect bounds;
    int area = 0;
};

inline std::vector<Candidate> findYellowCandidates(const cv::Mat& bgr) {
    if (bgr.empty() || bgr.type() != CV_8UC3) return {};

    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    cv::Mat mask;
    // The supplied calibration plate renders as an orange-yellow hue in OpenCV HSV.
    cv::inRange(hsv, cv::Scalar(5, 55, 45), cv::Scalar(20, 255, 255), mask);

    cv::Mat labels;
    cv::Mat statistics;
    cv::Mat centers;
    const int component_count = cv::connectedComponentsWithStats(
        mask, labels, statistics, centers, 8, CV_32S);

    std::vector<Candidate> candidates;
    candidates.reserve(static_cast<size_t>(component_count));
    for (int label = 1; label < component_count; ++label) {
        const int x = statistics.at<int>(label, cv::CC_STAT_LEFT);
        const int y = statistics.at<int>(label, cv::CC_STAT_TOP);
        const int width = statistics.at<int>(label, cv::CC_STAT_WIDTH);
        const int height = statistics.at<int>(label, cv::CC_STAT_HEIGHT);
        const int area = statistics.at<int>(label, cv::CC_STAT_AREA);
        if (area < 7500 || area > 10000 || width <= 0 || height <= 0) continue;
        const double aspect = static_cast<double>(width) / static_cast<double>(height);
        if (aspect < 0.65 || aspect > 1.5) continue;
        candidates.push_back({
            {static_cast<float>(centers.at<double>(label, 0)),
             static_cast<float>(centers.at<double>(label, 1))},
            {x, y, width, height}, area});
    }
    return candidates;
}

inline bool touchesVerticalBoundary(const Candidate& candidate, const cv::Size& image_size) {
    return candidate.bounds.y <= 0 || candidate.bounds.br().y >= image_size.height;
}

inline double rms(const std::vector<double>& residuals) {
    if (residuals.empty()) return std::numeric_limits<double>::infinity();
    double sum = 0.0;
    for (const double residual : residuals) sum += residual * residual;
    return std::sqrt(sum / static_cast<double>(residuals.size()));
}

inline bool solveLeastSquares(const cv::Mat& design, const cv::Mat& values, cv::Mat& coefficients) {
    return cv::solve(design, values, coefficients, cv::DECOMP_SVD) && cv::checkRange(coefficients);
}

inline bool jsonString(const std::string& text, const std::string& key, std::string& value) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) return false;
    value = match[1].str();
    return true;
}

inline bool jsonNumber(const std::string& text, const std::string& key, double& value) {
    const std::regex pattern(
        "\\\"" + key + "\\\"\\s*:\\s*"
        "(-?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) return false;
    try {
        value = std::stod(match[1].str());
    } catch (...) {
        return false;
    }
    return std::isfinite(value);
}

inline bool jsonNumberArray(
    const std::string& text, const std::string& key, const size_t expected_count,
    std::vector<double>& values) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\[([^\\]]*)\\]");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) return false;
    const std::regex number("-?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?");
    values.clear();
    const std::string body = match[1].str();
    for (std::sregex_iterator it(body.begin(), body.end(), number), end; it != end; ++it) {
        try {
            const double value = std::stod((*it)[0].str());
            if (!std::isfinite(value)) return false;
            values.push_back(value);
        } catch (...) {
            return false;
        }
    }
    return values.size() == expected_count;
}

} // namespace detail

inline bool detectYellowCenters(const cv::Mat& bgr, std::vector<cv::Point2f>& centers) {
    centers.clear();
    const std::vector<detail::Candidate> candidates = detail::findYellowCandidates(bgr);
    for (const detail::Candidate& candidate : candidates) {
        if (!detail::touchesVerticalBoundary(candidate, bgr.size())) centers.push_back(candidate.center);
    }
    return !centers.empty();
}

inline bool createCalibration(const cv::Mat& bgr, const double point_spacing_mm,
                              CalibrationResult& result, cv::Mat& diagnostic_bgr) {
    result = {};
    diagnostic_bgr.release();
    if (bgr.empty() || bgr.type() != CV_8UC3 || point_spacing_mm <= 0.0) return false;

    std::vector<detail::Candidate> candidates = detail::findYellowCandidates(bgr);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const detail::Candidate& candidate) {
        return detail::touchesVerticalBoundary(candidate, bgr.size());
    }), candidates.end());
    if (candidates.size() < 60U) return false;

    std::sort(candidates.begin(), candidates.end(), [](const detail::Candidate& left, const detail::Candidate& right) {
        return left.center.x < right.center.x;
    });
    std::vector<std::vector<detail::Candidate>> columns;
    for (const detail::Candidate& candidate : candidates) {
        if (columns.empty()) {
            columns.push_back({candidate});
            continue;
        }
        const std::vector<detail::Candidate>& current = columns.back();
        double mean_x = 0.0;
        for (const detail::Candidate& point : current) mean_x += point.center.x;
        mean_x /= static_cast<double>(current.size());
        if (std::abs(candidate.center.x - mean_x) <= 120.0) {
            columns.back().push_back(candidate);
        } else {
            columns.push_back({candidate});
        }
    }
    columns.erase(std::remove_if(columns.begin(), columns.end(), [](const std::vector<detail::Candidate>& column) {
        return column.size() < 3U;
    }), columns.end());
    if (columns.size() < 20U) return false;
    for (std::vector<detail::Candidate>& column : columns) {
        std::sort(column.begin(), column.end(), [](const detail::Candidate& left, const detail::Candidate& right) {
            return left.center.y < right.center.y;
        });
    }

    constexpr int kRows = 3;
    const int column_count = static_cast<int>(columns.size());
    const int point_count = column_count * kRows;
    cv::Mat affine_design(point_count, 3, CV_64F);
    cv::Mat x_quadratic_design(point_count, 3, CV_64F);
    cv::Mat source_x(point_count, 1, CV_64F);
    cv::Mat source_y(point_count, 1, CV_64F);
    int index = 0;
    for (int column = 0; column < column_count; ++column) {
        for (int row = 0; row < kRows; ++row) {
            const cv::Point2f center = columns[column][row].center;
            result.centers.push_back(center);
            result.lattice_indices.emplace_back(column, row);
            affine_design.at<double>(index, 0) = 1.0;
            affine_design.at<double>(index, 1) = static_cast<double>(column);
            affine_design.at<double>(index, 2) = static_cast<double>(row);
            x_quadratic_design.at<double>(index, 0) = 1.0;
            x_quadratic_design.at<double>(index, 1) = static_cast<double>(column);
            x_quadratic_design.at<double>(index, 2) = static_cast<double>(column * column);
            source_x.at<double>(index) = center.x;
            source_y.at<double>(index) = center.y;
            ++index;
        }
    }

    cv::Mat affine_x;
    cv::Mat affine_y;
    cv::Mat quadratic_x;
    cv::Mat linear_y;
    if (!detail::solveLeastSquares(affine_design, source_x, affine_x) ||
        !detail::solveLeastSquares(affine_design, source_y, affine_y) ||
        !detail::solveLeastSquares(x_quadratic_design, source_x, quadratic_x) ||
        !detail::solveLeastSquares(affine_design, source_y, linear_y)) {
        return false;
    }

    result.source_x_coefficients = {
        quadratic_x.at<double>(0), quadratic_x.at<double>(1), quadratic_x.at<double>(2)};
    result.source_y_coefficients = {
        linear_y.at<double>(0), linear_y.at<double>(1), linear_y.at<double>(2)};
    result.output_origin = {result.source_x_coefficients[0], result.source_y_coefficients[0]};
    result.output_pitch_pixels = affine_x.at<double>(1);
    result.pixels_per_mm_x = result.output_pitch_pixels / point_spacing_mm;
    result.pixels_per_mm_y = affine_y.at<double>(2) / point_spacing_mm;
    if (result.output_pitch_pixels <= 0.0 || result.pixels_per_mm_x <= 0.0 || result.pixels_per_mm_y <= 0.0) {
        return false;
    }

    std::vector<double> affine_x_residuals;
    std::vector<double> quadratic_x_residuals;
    std::vector<double> linear_y_residuals;
    affine_x_residuals.reserve(static_cast<size_t>(point_count));
    quadratic_x_residuals.reserve(static_cast<size_t>(point_count));
    linear_y_residuals.reserve(static_cast<size_t>(point_count));
    for (int point = 0; point < point_count; ++point) {
        const cv::Mat affine_x_value = affine_design.row(point) * affine_x;
        const cv::Mat quadratic_x_value = x_quadratic_design.row(point) * quadratic_x;
        const cv::Mat linear_y_value = affine_design.row(point) * linear_y;
        affine_x_residuals.push_back(source_x.at<double>(point) - affine_x_value.at<double>(0));
        quadratic_x_residuals.push_back(source_x.at<double>(point) - quadratic_x_value.at<double>(0));
        linear_y_residuals.push_back(source_y.at<double>(point) - linear_y_value.at<double>(0));
    }
    result.affine_x_rms_pixels = detail::rms(affine_x_residuals);
    result.fitted_x_rms_pixels = detail::rms(quadratic_x_residuals);
    result.y_affine_rms_pixels = detail::rms(linear_y_residuals);

    diagnostic_bgr = bgr.clone();
    for (int point = 0; point < point_count; ++point) {
        const cv::Point2f measured = result.centers[point];
        const cv::Point2i lattice = result.lattice_indices[point];
        const double fitted_x = result.source_x_coefficients[0] +
                                result.source_x_coefficients[1] * lattice.x +
                                result.source_x_coefficients[2] * lattice.x * lattice.x;
        const double fitted_y = result.source_y_coefficients[0] +
                                result.source_y_coefficients[1] * lattice.x +
                                result.source_y_coefficients[2] * lattice.y;
        const cv::Point predicted(cvRound(fitted_x), cvRound(fitted_y));
        cv::circle(diagnostic_bgr, measured, 15, cv::Scalar(0, 255, 0), 3, cv::LINE_AA);
        cv::arrowedLine(diagnostic_bgr, predicted, measured, cv::Scalar(0, 0, 255), 2, cv::LINE_AA, 0, 0.2);
        cv::putText(diagnostic_bgr, std::to_string(lattice.x) + "," + std::to_string(lattice.y),
                    measured + cv::Point2f(18.0F, -18.0F), cv::FONT_HERSHEY_SIMPLEX,
                    0.55, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
    }
    cv::putText(diagnostic_bgr, "X affine RMS=" + cv::format("%.3f", result.affine_x_rms_pixels) +
                " px, X quadratic RMS=" + cv::format("%.3f", result.fitted_x_rms_pixels) +
                " px, Y affine RMS=" + cv::format("%.3f", result.y_affine_rms_pixels) + " px",
                cv::Point(30, 60), cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
    return true;
}

inline bool correctImage(const cv::Mat& bgr, const CalibrationResult& calibration, cv::Mat& corrected_bgr) {
    corrected_bgr.release();
    if (bgr.empty() || bgr.type() != CV_8UC3 || calibration.output_pitch_pixels <= 0.0) return false;

    cv::Mat map_x(bgr.size(), CV_32FC1);
    cv::Mat map_y(bgr.size(), CV_32FC1);
    for (int y = 0; y < bgr.rows; ++y) {
        float* map_x_row = map_x.ptr<float>(y);
        float* map_y_row = map_y.ptr<float>(y);
        for (int x = 0; x < bgr.cols; ++x) {
            const double u = (static_cast<double>(x) - calibration.output_origin.x) / calibration.output_pitch_pixels;
            map_x_row[x] = static_cast<float>(calibration.source_x_coefficients[0] +
                                              calibration.source_x_coefficients[1] * u +
                                              calibration.source_x_coefficients[2] * u * u);
            map_y_row[x] = static_cast<float>(y);
        }
    }
    cv::remap(bgr, corrected_bgr, map_x, map_y, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar());
    return !corrected_bgr.empty();
}

inline bool saveCalibrationJson(
    const CalibrationResult& calibration, const cv::Size& calibration_image_size,
    const double point_spacing_mm, const std::filesystem::path& json_path) {
    if (calibration_image_size.width <= 0 || calibration_image_size.height <= 0 ||
        point_spacing_mm <= 0.0 || calibration.output_pitch_pixels <= 0.0 ||
        !std::isfinite(point_spacing_mm) || !std::isfinite(calibration.output_pitch_pixels)) {
        return false;
    }
    for (int index = 0; index < 3; ++index) {
        if (!std::isfinite(calibration.source_x_coefficients[index])) {
            return false;
        }
    }
    if (!std::isfinite(calibration.output_origin.x)) {
        return false;
    }

    std::ofstream file(json_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    file << std::setprecision(17)
         << "{\n"
         << "  \"schema\": \"line_scan_x_quadratic_only_v1\",\n"
         << "  \"input_width\": " << calibration_image_size.width << ",\n"
         << "  \"input_height\": " << calibration_image_size.height << ",\n"
         << "  \"input_type\": \"BGR8\",\n"
         << "  \"point_spacing_mm\": " << point_spacing_mm << ",\n"
         << "  \"source_x\": [" << calibration.source_x_coefficients[0] << ", "
         << calibration.source_x_coefficients[1] << ", " << calibration.source_x_coefficients[2] << "],\n"
         << "  \"output_origin_x\": " << calibration.output_origin.x << ",\n"
         << "  \"output_pitch_pixels\": " << calibration.output_pitch_pixels << ",\n"
         << "  \"affine_x_rms_pixels\": " << calibration.affine_x_rms_pixels << ",\n"
         << "  \"fitted_x_rms_pixels\": " << calibration.fitted_x_rms_pixels << "\n"
         << "}\n";
    return file.good();
}

inline bool loadCalibrationJson(
    const std::filesystem::path& json_path, CalibrationResult& calibration,
    cv::Size& calibration_image_size, double& point_spacing_mm) {
    calibration = {};
    calibration_image_size = {};
    point_spacing_mm = 0.0;
    std::ifstream file(json_path, std::ios::binary);
    if (!file.is_open()) return false;
    std::ostringstream stream;
    stream << file.rdbuf();
    const std::string text = stream.str();

    std::string schema;
    std::string input_type;
    double width = 0.0;
    double height = 0.0;
    double output_pitch = 0.0;
    double output_origin_x = 0.0;
    std::vector<double> source_x;
    if (!detail::jsonString(text, "schema", schema) ||
        !detail::jsonString(text, "input_type", input_type) ||
        schema != "line_scan_x_quadratic_only_v1" || input_type != "BGR8" ||
        !detail::jsonNumber(text, "input_width", width) ||
        !detail::jsonNumber(text, "input_height", height) ||
        !detail::jsonNumber(text, "point_spacing_mm", point_spacing_mm) ||
        !detail::jsonNumber(text, "output_pitch_pixels", output_pitch) ||
        !detail::jsonNumber(text, "output_origin_x", output_origin_x) ||
        !detail::jsonNumberArray(text, "source_x", 3U, source_x) ||
        !std::isfinite(output_origin_x)) {
        return false;
    }
    if (width <= 0.0 || height <= 0.0 || point_spacing_mm <= 0.0 || output_pitch <= 0.0 ||
        std::floor(width) != width || std::floor(height) != height ||
        width > static_cast<double>(std::numeric_limits<int>::max()) ||
        height > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }

    calibration_image_size = {static_cast<int>(width), static_cast<int>(height)};
    calibration.source_x_coefficients = {source_x[0], source_x[1], source_x[2]};
    calibration.output_origin = {output_origin_x, 0.0};
    calibration.output_pitch_pixels = output_pitch;
    double quality = 0.0;
    if (detail::jsonNumber(text, "affine_x_rms_pixels", quality)) calibration.affine_x_rms_pixels = quality;
    if (detail::jsonNumber(text, "fitted_x_rms_pixels", quality)) calibration.fitted_x_rms_pixels = quality;
    calibration.pixels_per_mm_x = output_pitch / point_spacing_mm;
    return calibration.pixels_per_mm_x > 0.0;
}

inline bool correctImageByCalibrationFile(
    const cv::Mat& bgr, const CalibrationResult& calibration,
    const cv::Size& calibration_image_size, cv::Mat& corrected_bgr) {
    corrected_bgr.release();
    if (bgr.empty() || bgr.type() != CV_8UC3 || calibration_image_size.width <= 0 ||
        bgr.cols != calibration_image_size.width) {
        return false;
    }
    return correctImage(bgr, calibration, corrected_bgr);
}

} // namespace line_scan_color_distortion
