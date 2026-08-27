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

std::filesystem::path findAngleImageDirectory() {
    std::filesystem::path directory = std::filesystem::absolute(std::filesystem::current_path());
    const std::filesystem::path relative_directory = std::filesystem::u8path("images/根据线输出角度");
    while (true) {
        const std::filesystem::path candidate = directory / relative_directory;
        if (std::filesystem::is_regular_file(candidate / std::filesystem::u8path("角度调整2.bmp"))) {
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

int processImage(const std::filesystem::path& directory, const std::string& input_name, const std::string& output_name) {
    const cv::Mat mono8 = wafer_calib::readImageUnicode(
        toUtf8Path(directory / std::filesystem::u8path(input_name)), cv::IMREAD_GRAYSCALE);
    if (mono8.empty() || mono8.type() != CV_8UC1) {
        std::cerr << "读取失败: " << input_name << std::endl;
        return 1;
    }

    std::vector<unsigned char> result_buffer(static_cast<size_t>(mono8.total()) * 3U);
    double angle_degrees = 0.0;
    const int result = Wafer_FindHorizontalLineAngle(
        mono8.data, mono8.cols, mono8.rows, &angle_degrees, result_buffer.data());
    if (result != WAFER_SUCCESS) {
        std::cerr << "检测失败: " << input_name << "，错误码: " << result << std::endl;
        return result;
    }

    std::cout << input_name << " 的水平线角度: " << angle_degrees << "°" << std::endl;
    const cv::Mat result_bgr(mono8.rows, mono8.cols, CV_8UC3, result_buffer.data());
    const std::filesystem::path output_path = directory / std::filesystem::u8path(output_name);
    if (!writeImageUnicode(output_path, result_bgr)) {
        std::cerr << "结果图保存失败: " << toUtf8Path(output_path) << std::endl;
        return 2;
    }
    std::cout << "结果图已保存: " << toUtf8Path(output_path) << std::endl;
    return 0;
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    const std::filesystem::path directory = findAngleImageDirectory();
    if (directory.empty()) {
        std::cerr << "未找到角度调整输入图目录。" << std::endl;
        return 1;
    }

    const int first_result = processImage(directory, "角度调整2.bmp", "output_angle_adjustment_2.bmp");
    if (first_result != 0) return first_result;
    return processImage(directory, "角度调整1.bmp", "output_angle_adjustment_1.bmp");
}
