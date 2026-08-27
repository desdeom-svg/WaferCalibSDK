#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <opencv2/imgproc.hpp>

#include "line_scan_color_distortion.hpp"
#include "wafer_calib/wafer_calib.hpp"

namespace {

struct GridMetrics {
    int detected_center_count = 0;
    int column_count = 0;
    double row_slope = 0.0;
    double horizontal_spacing_stddev = 0.0;
};

bool writeImageUnicode(const std::filesystem::path& path, const cv::Mat& image) {
    std::vector<uchar> encoded;
    if (!cv::imencode(".bmp", image, encoded)) return false;
#ifdef _WIN32
    std::ofstream file(wafer_calib::stringToWstring(path.u8string()), std::ios::binary);
#else
    std::ofstream file(path, std::ios::binary);
#endif
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    return file.good();
}

bool measureYellowGrid(const cv::Mat& bgr, GridMetrics& metrics) {
    metrics = {};
    if (bgr.empty() || bgr.type() != CV_8UC3) return false;

    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    cv::Mat mask;
    cv::inRange(hsv, cv::Scalar(5, 55, 45), cv::Scalar(20, 255, 255), mask);
    cv::Mat labels;
    cv::Mat statistics;
    cv::Mat centers;
    const int count = cv::connectedComponentsWithStats(mask, labels, statistics, centers, 8, CV_32S);

    std::vector<cv::Point2f> points;
    for (int label = 1; label < count; ++label) {
        const int x = statistics.at<int>(label, cv::CC_STAT_LEFT);
        const int y = statistics.at<int>(label, cv::CC_STAT_TOP);
        const int width = statistics.at<int>(label, cv::CC_STAT_WIDTH);
        const int height = statistics.at<int>(label, cv::CC_STAT_HEIGHT);
        const int area = statistics.at<int>(label, cv::CC_STAT_AREA);
        const double aspect = height > 0 ? static_cast<double>(width) / height : 0.0;
        if (area < 5000 || area > 11000 || aspect < 0.65 || aspect > 1.5 ||
            y <= 0 || y + height >= bgr.rows) {
            continue;
        }
        points.emplace_back(
            static_cast<float>(centers.at<double>(label, 0)),
            static_cast<float>(centers.at<double>(label, 1)));
    }
    metrics.detected_center_count = static_cast<int>(points.size());
    if (points.size() < 60U) return false;

    std::sort(points.begin(), points.end(), [](const cv::Point2f& left, const cv::Point2f& right) {
        return left.x < right.x;
    });
    std::vector<std::vector<cv::Point2f>> columns;
    for (const cv::Point2f& point : points) {
        if (columns.empty()) {
            columns.push_back({point});
            continue;
        }
        const std::vector<cv::Point2f>& column = columns.back();
        double mean_x = 0.0;
        for (const cv::Point2f& value : column) mean_x += value.x;
        mean_x /= static_cast<double>(column.size());
        if (std::abs(point.x - mean_x) <= 120.0) columns.back().push_back(point);
        else columns.push_back({point});
    }
    columns.erase(std::remove_if(columns.begin(), columns.end(), [](const std::vector<cv::Point2f>& column) {
        return column.size() < 3U;
    }), columns.end());
    if (columns.size() < 20U) return false;

    std::vector<cv::Point2f> first_row;
    first_row.reserve(columns.size());
    for (std::vector<cv::Point2f>& column : columns) {
        std::sort(column.begin(), column.end(), [](const cv::Point2f& left, const cv::Point2f& right) {
            return left.y < right.y;
        });
        first_row.push_back(column.front());
    }
    cv::Vec4f line;
    cv::fitLine(first_row, line, cv::DIST_L2, 0.0, 0.01, 0.01);
    if (std::abs(line[0]) < 1e-8F) return false;
    std::sort(first_row.begin(), first_row.end(), [](const cv::Point2f& left, const cv::Point2f& right) {
        return left.x < right.x;
    });
    std::vector<double> spacing;
    for (size_t index = 1; index < first_row.size(); ++index) {
        spacing.push_back(first_row[index].x - first_row[index - 1U].x);
    }
    const double mean = std::accumulate(spacing.begin(), spacing.end(), 0.0) / spacing.size();
    double square_sum = 0.0;
    for (const double value : spacing) square_sum += (value - mean) * (value - mean);
    metrics.column_count = static_cast<int>(columns.size());
    metrics.row_slope = line[1] / line[0];
    metrics.horizontal_spacing_stddev = std::sqrt(square_sum / spacing.size());
    return true;
}

int run(
    const std::filesystem::path& input_path, const std::filesystem::path& calibration_path,
    const std::filesystem::path& output_path) {
    line_scan_color_distortion::CalibrationResult calibration;
    cv::Size calibration_size;
    double point_spacing_mm = 0.0;
    if (!line_scan_color_distortion::loadCalibrationJson(
            calibration_path, calibration, calibration_size, point_spacing_mm)) {
        std::cerr << "加载畸变配方失败: " << calibration_path.u8string() << std::endl;
        return 1;
    }
    const cv::Mat input = wafer_calib::readImageUnicode(input_path.u8string(), cv::IMREAD_COLOR);
    if (input.empty() || input.type() != CV_8UC3) {
        std::cerr << "读取 BGR 输入图失败: " << input_path.u8string() << std::endl;
        return 2;
    }
    cv::Mat corrected;
    if (!line_scan_color_distortion::correctImageByCalibrationFile(
            input, calibration, calibration_size, corrected)) {
        std::cerr << "输入图不兼容：必须为 BGR8，且宽度等于 " << calibration_size.width << "。" << std::endl;
        return 3;
    }
    if (!writeImageUnicode(output_path, corrected)) {
        std::cerr << "保存校正图失败: " << output_path.u8string() << std::endl;
        return 4;
    }

    GridMetrics before;
    GridMetrics after;
    const bool can_measure_before = measureYellowGrid(input, before);
    const bool can_measure_after = measureYellowGrid(corrected, after);
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "校正图: " << output_path.u8string() << std::endl;
    if (!can_measure_before || !can_measure_after) {
        std::cout << "仅完成兼容性应用，未完成点阵几何验证。" << std::endl;
        return 0;
    }
    std::cout << "检测圆心: " << before.detected_center_count << " -> " << after.detected_center_count << std::endl;
    std::cout << "有效列数: " << before.column_count << " -> " << after.column_count << std::endl;
    std::cout << "行斜率: " << before.row_slope << " -> " << after.row_slope << std::endl;
    std::cout << "行倾角: " << std::atan(before.row_slope) * 180.0 / CV_PI
              << " -> " << std::atan(after.row_slope) * 180.0 / CV_PI << " deg" << std::endl;
    std::cout << "横向点距标准差: " << before.horizontal_spacing_stddev
              << " -> " << after.horizontal_spacing_stddev << " px" << std::endl;
    // This recipe corrects X only.  A changed row slope would indicate an unintended Y shear.
    const bool slope_preserved = std::abs(after.row_slope - before.row_slope) <= 0.001;
    const bool spacing_improved = after.horizontal_spacing_stddev <= before.horizontal_spacing_stddev;
    std::cout << "倾角保持: " << (slope_preserved ? "通过" : "未通过") << std::endl;
    std::cout << "几何验证: " << (slope_preserved && spacing_improved ? "通过" : "未通过") << std::endl;
    return slope_preserved && spacing_improved ? 0 : 5;
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(65001);
    if (argc == 4) return run(std::filesystem::path(argv[1]), std::filesystem::path(argv[2]), std::filesystem::path(argv[3]));
#else
int main(int argc, char* argv[]) {
    if (argc == 4) return run(
        std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[2]), std::filesystem::u8path(argv[3]));
#endif
    std::cerr << "用法: sample_line_scan_apply_analysis.exe <输入图.bmp> <配方.json> <输出图.bmp>\n";
    return 6;
}
