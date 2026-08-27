#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "../samples/line_scan_color_distortion.hpp"
#include "wafer_calib/wafer_calib.hpp"

namespace {

std::filesystem::path findImagePath() {
    std::filesystem::path directory = std::filesystem::absolute(std::filesystem::current_path());
    const std::filesystem::path relative = std::filesystem::u8path("images/线扫相机畸变矫正/1.bmp");
    while (true) {
        const std::filesystem::path candidate = directory / relative;
        if (std::filesystem::is_regular_file(candidate)) return candidate;
        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory) return {};
        directory = parent;
    }
}

double lineSlope(const std::vector<cv::Point2f>& centers) {
    if (centers.size() < 2U) return std::numeric_limits<double>::infinity();
    std::vector<cv::Point2f> sorted = centers;
    std::sort(sorted.begin(), sorted.end(), [](const cv::Point2f& left, const cv::Point2f& right) {
        return left.y < right.y;
    });

    // The first row has the smallest Y values after correction. Fit its points to y = k*x + b.
    const float row_y = sorted.front().y;
    std::vector<cv::Point2f> row;
    for (const cv::Point2f& point : sorted) {
        if (std::abs(point.y - row_y) < 100.0F) row.push_back(point);
    }
    if (row.size() < 10U) return std::numeric_limits<double>::infinity();
    cv::Vec4f line;
    cv::fitLine(row, line, cv::DIST_L2, 0.0, 0.01, 0.01);
    return static_cast<double>(line[1] / line[0]);
}

double horizontalSpacingStandardDeviation(const std::vector<cv::Point2f>& centers) {
    std::vector<cv::Point2f> sorted = centers;
    std::sort(sorted.begin(), sorted.end(), [](const cv::Point2f& left, const cv::Point2f& right) {
        return left.y < right.y;
    });
    if (sorted.empty()) return std::numeric_limits<double>::infinity();
    const float row_y = sorted.front().y;
    std::vector<cv::Point2f> row;
    for (const cv::Point2f& point : sorted) {
        if (std::abs(point.y - row_y) < 100.0F) row.push_back(point);
    }
    std::sort(row.begin(), row.end(), [](const cv::Point2f& left, const cv::Point2f& right) {
        return left.x < right.x;
    });
    if (row.size() < 10U) return std::numeric_limits<double>::infinity();
    std::vector<double> spacing;
    for (size_t index = 1; index < row.size(); ++index) {
        spacing.push_back(static_cast<double>(row[index].x - row[index - 1U].x));
    }
    const double mean = std::accumulate(spacing.begin(), spacing.end(), 0.0) / spacing.size();
    double sum = 0.0;
    for (const double value : spacing) sum += (value - mean) * (value - mean);
    return std::sqrt(sum / spacing.size());
}

} // namespace

int main() {
    const std::filesystem::path image_path = findImagePath();
    const cv::Mat input = wafer_calib::readImageUnicode(image_path.u8string(), cv::IMREAD_COLOR);
    if (input.empty() || input.type() != CV_8UC3) return 1;

    line_scan_color_distortion::CalibrationResult calibration;
    cv::Mat diagnostic;
    if (!line_scan_color_distortion::createCalibration(input, 5.0, calibration, diagnostic)) return 2;
    if (calibration.centers.size() < 60U || calibration.lattice_indices.size() != calibration.centers.size()) return 3;
    if (calibration.affine_x_rms_pixels < 1.0) return 4;
    if (calibration.fitted_x_rms_pixels >= calibration.affine_x_rms_pixels * 0.35) return 5;
    if (calibration.y_affine_rms_pixels > 0.5 || diagnostic.type() != CV_8UC3) return 6;

    cv::Mat corrected;
    if (!line_scan_color_distortion::correctImage(input, calibration, corrected)) return 7;
    if (corrected.type() != CV_8UC3 || corrected.size() != input.size()) return 8;

    std::vector<cv::Point2f> corrected_centers;
    if (!line_scan_color_distortion::detectYellowCenters(corrected, corrected_centers) || corrected_centers.size() < 60U) {
        return 9;
    }
    const double before_slope = lineSlope(calibration.centers);
    const double after_slope = lineSlope(corrected_centers);
    // X-only correction must not rotate/shear the image: fitted row slope stays unchanged.
    if (!std::isfinite(after_slope) || std::abs(after_slope - before_slope) > 0.001) return 10;

    const double before_spacing_stddev = horizontalSpacingStandardDeviation(calibration.centers);
    const double after_spacing_stddev = horizontalSpacingStandardDeviation(corrected_centers);
    if (!std::isfinite(after_spacing_stddev) || after_spacing_stddev > before_spacing_stddev) return 11;

    const std::filesystem::path json_path =
        std::filesystem::temp_directory_path() / "line_scan_calibration_test.json";
    if (!line_scan_color_distortion::saveCalibrationJson(calibration, input.size(), 5.0, json_path)) return 12;
    std::ifstream json_file(json_path, std::ios::binary);
    std::ostringstream json_stream;
    json_stream << json_file.rdbuf();
    const std::string json_text = json_stream.str();
    if (json_text.find("\"schema\": \"line_scan_x_quadratic_only_v1\"") == std::string::npos ||
        json_text.find("\"source_y\"") != std::string::npos) {
        return 13;
    }
    line_scan_color_distortion::CalibrationResult loaded_calibration;
    cv::Size calibration_image_size;
    double loaded_point_spacing_mm = 0.0;
    if (!line_scan_color_distortion::loadCalibrationJson(
            json_path, loaded_calibration, calibration_image_size, loaded_point_spacing_mm)) {
        return 14;
    }
    if (calibration_image_size != input.size() || std::abs(loaded_point_spacing_mm - 5.0) > 1e-12) return 15;
    cv::Mat corrected_from_json;
    if (!line_scan_color_distortion::correctImageByCalibrationFile(
            input, loaded_calibration, calibration_image_size, corrected_from_json)) {
        return 16;
    }
    if (cv::norm(corrected - corrected_from_json, cv::NORM_INF) != 0.0) return 17;
    const cv::Mat wrong_width(input.rows, input.cols - 1, CV_8UC3, cv::Scalar());
    if (line_scan_color_distortion::correctImageByCalibrationFile(
            wrong_width, loaded_calibration, calibration_image_size, corrected_from_json)) {
        return 18;
    }

    const std::filesystem::path legacy_json_path =
        std::filesystem::temp_directory_path() / "line_scan_calibration_legacy_test.json";
    std::string legacy_json = json_text;
    const std::string current_schema = "line_scan_x_quadratic_only_v1";
    const std::string legacy_schema = "line_scan_x_quadratic_y_affine_v1";
    const size_t schema_position = legacy_json.find(current_schema);
    if (schema_position == std::string::npos) return 19;
    legacy_json.replace(schema_position, current_schema.size(), legacy_schema);
    std::ofstream legacy_json_file(legacy_json_path, std::ios::binary | std::ios::trunc);
    legacy_json_file << legacy_json;
    legacy_json_file.close();
    if (line_scan_color_distortion::loadCalibrationJson(
            legacy_json_path, loaded_calibration, calibration_image_size, loaded_point_spacing_mm)) {
        return 20;
    }
    std::error_code remove_error;
    std::filesystem::remove(json_path, remove_error);
    std::filesystem::remove(legacy_json_path, remove_error);

    std::cout << "centers=" << calibration.centers.size()
              << ", affine_x_rms=" << calibration.affine_x_rms_pixels
              << ", quadratic_x_rms=" << calibration.fitted_x_rms_pixels
              << ", y_rms=" << calibration.y_affine_rms_pixels << std::endl;
    return 0;
}
