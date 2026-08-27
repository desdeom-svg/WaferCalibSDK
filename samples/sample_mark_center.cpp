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

std::filesystem::path findMarkImagePath() {
    std::filesystem::path directory = std::filesystem::absolute(std::filesystem::current_path());
    const std::filesystem::path relative_image_path = std::filesystem::u8path(
        "images/根据四个十字Mark求中心（兼容不同倍率，Mark大小不一样）/buffer-Mono8-4096x4096.bmp");

    while (true) {
        const std::filesystem::path candidate = directory / relative_image_path;
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }

        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory) {
            return {};
        }
        directory = parent;
    }
}

std::string toUtf8Path(const std::filesystem::path& path) {
    return path.u8string();
}

bool writeImageUnicode(const std::filesystem::path& output_path, const cv::Mat& image) {
    std::vector<uchar> encoded;
    if (!cv::imencode(".bmp", image, encoded)) {
        return false;
    }

#ifdef _WIN32
    std::ofstream output_file(wafer_calib::stringToWstring(toUtf8Path(output_path)), std::ios::binary);
#else
    std::ofstream output_file(output_path, std::ios::binary);
#endif
    if (!output_file.is_open()) {
        return false;
    }
    output_file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    return output_file.good();
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    const std::filesystem::path image_path = findMarkImagePath();
    if (image_path.empty()) {
        std::cerr << "未找到十字 Mark 输入图。" << std::endl;
        return 1;
    }

    const cv::Mat mono8 = wafer_calib::readImageUnicode(toUtf8Path(image_path), cv::IMREAD_GRAYSCALE);
    if (mono8.empty() || mono8.type() != CV_8UC1) {
        std::cerr << "输入图像读取失败或不是 Mono8。" << std::endl;
        return 2;
    }

    std::vector<unsigned char> result_buffer(static_cast<size_t>(mono8.total()) * 3U);
    double calculated_center_x = 0.0;
    double calculated_center_y = 0.0;
    const int result = Wafer_FindFourCrossMarkCenter(
        mono8.data,
        mono8.cols,
        mono8.rows,
        &calculated_center_x,
        &calculated_center_y,
        result_buffer.data());
    if (result != WAFER_SUCCESS) {
        std::cerr << "十字 Mark 检测失败，错误码: " << result << std::endl;
        return result;
    }

    const double image_center_x = (mono8.cols - 1) / 2.0;
    const double image_center_y = (mono8.rows - 1) / 2.0;
    std::cout << "计算中心: (" << calculated_center_x << ", " << calculated_center_y << ")" << std::endl;
    std::cout << "图像中心: (" << image_center_x << ", " << image_center_y << ")" << std::endl;
    std::cout << "差异: dx=" << calculated_center_x - image_center_x
              << ", dy=" << calculated_center_y - image_center_y << std::endl;

    const cv::Mat result_bgr(mono8.rows, mono8.cols, CV_8UC3, result_buffer.data());
    const std::filesystem::path output_path = image_path.parent_path() / "output_mark_center.bmp";
    if (!writeImageUnicode(output_path, result_bgr)) {
        std::cerr << "结果图保存失败: " << toUtf8Path(output_path) << std::endl;
        return 3;
    }

    std::cout << "结果图已保存: " << toUtf8Path(output_path) << std::endl;
    return 0;
}
