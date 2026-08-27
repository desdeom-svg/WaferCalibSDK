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

#include "wafer_calib/c_api/wafer_calib_c.h"
#include "wafer_calib/wafer_calib.hpp"

namespace {

std::filesystem::path findDistortionImageDirectory() {
    std::filesystem::path directory = std::filesystem::absolute(std::filesystem::current_path());
    const std::filesystem::path relative_directory = std::filesystem::u8path("images/畸变矫正");
    while (true) {
        const std::filesystem::path candidate = directory / relative_directory;
        if (std::filesystem::is_regular_file(candidate / std::filesystem::u8path("点阵.bmp"))) {
            return candidate;
        }
        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory) return {};
        directory = parent;
    }
}

std::string toUtf8Path(const std::filesystem::path& path) {
    return path.u8string();
}

bool writeImageUnicode(const std::filesystem::path& path, const cv::Mat& image) {
    std::vector<uchar> encoded;
    if (!cv::imencode(".bmp", image, encoded)) return false;
#ifdef _WIN32
    std::ofstream file(wafer_calib::stringToWstring(toUtf8Path(path)), std::ios::binary);
#else
    std::ofstream file(path, std::ios::binary);
#endif
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    return file.good();
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    const std::filesystem::path directory = findDistortionImageDirectory();
    if (directory.empty()) {
        std::cerr << "未找到畸变矫正图像目录。" << std::endl;
        return 1;
    }

    const cv::Mat mono8 = wafer_calib::readImageUnicode(
        toUtf8Path(directory / std::filesystem::u8path("点阵.bmp")), cv::IMREAD_GRAYSCALE);
    if (mono8.empty() || mono8.type() != CV_8UC1) {
        std::cerr << "读取点阵图失败。" << std::endl;
        return 2;
    }

    WaferDotGridDistortionTemplate distortion_template{};
    std::vector<unsigned char> diagnostic_buffer(static_cast<size_t>(mono8.total()) * 3U);
    const int build_status = Wafer_CreateDotGridDistortionTemplate(
        mono8.data, mono8.cols, mono8.rows, 10, 10, 0.15,
        &distortion_template, diagnostic_buffer.data());
    if (build_status != WAFER_SUCCESS) {
        std::cerr << "建立畸变矫正模板失败，错误码: " << build_status << std::endl;
        return build_status;
    }

    std::vector<unsigned char> corrected_buffer(mono8.total());
    const int correct_status = Wafer_CorrectImageByDotGridTemplate(
        mono8.data, mono8.cols, mono8.rows, &distortion_template, corrected_buffer.data());
    if (correct_status != WAFER_SUCCESS) {
        std::cerr << "应用畸变矫正模板失败，错误码: " << correct_status << std::endl;
        return correct_status;
    }

    const cv::Mat diagnostic_bgr(mono8.rows, mono8.cols, CV_8UC3, diagnostic_buffer.data());
    const cv::Mat corrected_mono8(mono8.rows, mono8.cols, CV_8UC1, corrected_buffer.data());
    const std::filesystem::path diagnostic_path = directory / std::filesystem::u8path("output_distortion_detection.bmp");
    const std::filesystem::path corrected_path = directory / std::filesystem::u8path("output_distortion_corrected.bmp");
    if (!writeImageUnicode(diagnostic_path, diagnostic_bgr) ||
        !writeImageUnicode(corrected_path, corrected_mono8)) {
        std::cerr << "保存畸变矫正结果图失败。" << std::endl;
        return 3;
    }

    std::cout << "有效圆点: " << distortion_template.detected_point_count << std::endl;
    std::cout << "RMS 残差: " << distortion_template.rms_error_pixels << " px" << std::endl;
    std::cout << "检测结果图: " << toUtf8Path(diagnostic_path) << std::endl;
    std::cout << "校正结果图: " << toUtf8Path(corrected_path) << std::endl;
    return 0;
}
