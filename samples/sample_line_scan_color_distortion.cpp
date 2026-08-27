#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "line_scan_color_distortion.hpp"
#include "wafer_calib/wafer_calib.hpp"

namespace {

std::filesystem::path findDefaultImagePath() {
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

int calibrateAndWrite(
    const std::filesystem::path& input_path,
    const std::filesystem::path* json_path) {
    if (input_path.empty()) {
        std::cerr << "未找到默认线扫标定图。" << std::endl;
        return 1;
    }
    const cv::Mat input_bgr = wafer_calib::readImageUnicode(input_path.u8string(), cv::IMREAD_COLOR);
    if (input_bgr.empty() || input_bgr.type() != CV_8UC3) {
        std::cerr << "读取 BGR 标定图失败: " << input_path.u8string() << std::endl;
        return 2;
    }

    line_scan_color_distortion::CalibrationResult calibration;
    cv::Mat diagnostic_bgr;
    if (!line_scan_color_distortion::createCalibration(input_bgr, 5.0, calibration, diagnostic_bgr)) {
        std::cerr << "建立黄色点阵校正模型失败。" << std::endl;
        return 3;
    }
    cv::Mat corrected_bgr;
    if (!line_scan_color_distortion::correctImage(input_bgr, calibration, corrected_bgr)) {
        std::cerr << "执行 BGR 畸变校正失败。" << std::endl;
        return 4;
    }

    const std::filesystem::path output_directory = input_path.parent_path();
    const std::filesystem::path diagnostic_path = output_directory / std::filesystem::u8path("output_line_scan_detection.bmp");
    const std::filesystem::path corrected_path = output_directory / std::filesystem::u8path("output_line_scan_corrected.bmp");
    if (!writeImageUnicode(diagnostic_path, diagnostic_bgr) || !writeImageUnicode(corrected_path, corrected_bgr)) {
        std::cerr << "保存线扫校正结果图失败。" << std::endl;
        return 5;
    }
    if (json_path && !line_scan_color_distortion::saveCalibrationJson(
            calibration, input_bgr.size(), 5.0, *json_path)) {
        std::cerr << "保存畸变配方 JSON 失败: " << json_path->u8string() << std::endl;
        return 6;
    }

    std::cout << "有效黄色圆点: " << calibration.centers.size() << std::endl;
    std::cout << "X 仿射 RMS: " << calibration.affine_x_rms_pixels << " px" << std::endl;
    std::cout << "X 二次拟合 RMS: " << calibration.fitted_x_rms_pixels << " px" << std::endl;
    std::cout << "X 标定分辨率: " << calibration.pixels_per_mm_x << " px/mm" << std::endl;
    std::cout << "诊断图: " << diagnostic_path.u8string() << std::endl;
    std::cout << "校正图: " << corrected_path.u8string() << std::endl;
    if (json_path) std::cout << "畸变配方: " << json_path->u8string() << std::endl;
    return 0;
}

int applyAndWrite(
    const std::filesystem::path& json_path, const std::filesystem::path& input_path,
    const std::filesystem::path& output_path) {
    line_scan_color_distortion::CalibrationResult calibration;
    cv::Size calibration_image_size;
    double point_spacing_mm = 0.0;
    if (!line_scan_color_distortion::loadCalibrationJson(
            json_path, calibration, calibration_image_size, point_spacing_mm)) {
        std::cerr << "加载畸变配方 JSON 失败: " << json_path.u8string() << std::endl;
        return 7;
    }
    const cv::Mat input_bgr = wafer_calib::readImageUnicode(input_path.u8string(), cv::IMREAD_COLOR);
    if (input_bgr.empty() || input_bgr.type() != CV_8UC3) {
        std::cerr << "读取 BGR 应用图失败: " << input_path.u8string() << std::endl;
        return 8;
    }
    cv::Mat corrected_bgr;
    if (!line_scan_color_distortion::correctImageByCalibrationFile(
            input_bgr, calibration, calibration_image_size, corrected_bgr)) {
        std::cerr << "应用图与畸变配方不兼容：必须为 BGR8，且宽度等于 "
                  << calibration_image_size.width << "。" << std::endl;
        return 9;
    }
    if (!writeImageUnicode(output_path, corrected_bgr)) {
        std::cerr << "保存应用校正图失败: " << output_path.u8string() << std::endl;
        return 10;
    }
    std::cout << "已加载畸变配方: " << json_path.u8string() << std::endl;
    std::cout << "标定尺寸: " << calibration_image_size.width << "x" << calibration_image_size.height << std::endl;
    std::cout << "应用图尺寸: " << input_bgr.cols << "x" << input_bgr.rows << std::endl;
    std::cout << "校正图: " << output_path.u8string() << std::endl;
    return 0;
}

} // namespace

// Windows uses wmain so Chinese image and JSON paths are received losslessly.
#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(65001);
    if (argc == 1) {
        return calibrateAndWrite(findDefaultImagePath(), nullptr);
    }
    const std::wstring mode = argv[1];
    if (mode == L"--calibrate" && argc == 4) {
        const std::filesystem::path input_path(argv[2]);
        const std::filesystem::path json_path(argv[3]);
        return calibrateAndWrite(input_path, &json_path);
    }
    if (mode == L"--apply" && argc == 5) {
        return applyAndWrite(
            std::filesystem::path(argv[2]), std::filesystem::path(argv[3]),
            std::filesystem::path(argv[4]));
    }
#else
int main(int argc, char* argv[]) {
    if (argc == 1) {
        return calibrateAndWrite(findDefaultImagePath(), nullptr);
    }
    const std::string mode = argv[1];
    if (mode == "--calibrate" && argc == 4) {
        const std::filesystem::path input_path = std::filesystem::u8path(argv[2]);
        const std::filesystem::path json_path = std::filesystem::u8path(argv[3]);
        return calibrateAndWrite(input_path, &json_path);
    }
    if (mode == "--apply" && argc == 5) {
        return applyAndWrite(
            std::filesystem::u8path(argv[2]), std::filesystem::u8path(argv[3]),
            std::filesystem::u8path(argv[4]));
    }
#endif

    std::cerr << "用法:\n"
              << "  sample_line_scan_color_distortion.exe\n"
              << "  sample_line_scan_color_distortion.exe --calibrate <标定图.bmp> <配方.json>\n"
              << "  sample_line_scan_color_distortion.exe --apply <配方.json> <输入图.bmp> <输出图.bmp>\n";
    return 11;
}
