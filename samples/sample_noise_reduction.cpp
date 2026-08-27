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

std::filesystem::path findNoiseImageDirectory() {
    std::filesystem::path directory = std::filesystem::absolute(std::filesystem::current_path());
    const std::filesystem::path relative_image_directory = std::filesystem::u8path("images/去噪声");
    const std::filesystem::path first_image_name = std::filesystem::u8path("背景噪声.bmp");

    while (true) {
        const std::filesystem::path candidate = directory / relative_image_directory;
        if (std::filesystem::is_regular_file(candidate / first_image_name)) {
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

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    std::cout << "=== [WaferCalibSDK] N 帧暗场背景噪声模板合成测试 ===" << std::endl;

    const std::filesystem::path noise_image_directory = findNoiseImageDirectory();
    if (noise_image_directory.empty()) {
        std::cerr << "未找到 images/去噪声/背景噪声.bmp；请从 SDK 根目录或 build 子目录运行。" << std::endl;
        return 1;
    }

    std::vector<std::string> dark_files;
    dark_files.reserve(10);
    for (int index = 0; index < 10; ++index) {
        const std::string file_name = index == 0
            ? "背景噪声.bmp"
            : "背景噪声" + std::to_string(index) + ".bmp";
        dark_files.push_back(toUtf8Path(noise_image_directory / std::filesystem::u8path(file_name)));
    }

    std::vector<cv::Mat> dark_frames;
    dark_frames.reserve(dark_files.size());
    for (const auto& path : dark_files) {
        cv::Mat image = wafer_calib::readImageUnicode(path, cv::IMREAD_GRAYSCALE);
        if (!image.empty()) {
            dark_frames.push_back(image);
        }
    }

    std::cout << "成功读取 " << dark_frames.size() << " 帧暗场图像进行合成测试。" << std::endl;
    if (dark_frames.size() != dark_files.size()) {
        std::cerr << "暗场图像未全部读取成功：期望 " << dark_files.size()
                  << " 帧，实际 " << dark_frames.size() << " 帧。" << std::endl;
        return 2;
    }

    cv::Mat dark_template_cpp;
    const wafer_calib::Status status = wafer_calib::NoiseReductionModule::createDarkFrameTemplate(
        dark_frames, dark_template_cpp, wafer_calib::FrameCombineMethod::Median);
    if (!status.ok()) {
        std::cerr << "[C++ 接口合成失败] 错误码: " << static_cast<int>(status.code)
                  << "，原因: " << status.message << std::endl;
        return 3;
    }

    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(dark_template_cpp, mean, stddev);
    std::cout << "\n[C++ 接口合成成功] 暗场中值模板均值: " << mean[0]
              << " | 标准差: " << stddev[0] << std::endl;

    std::vector<uchar> buffer;
    cv::imencode(".bmp", dark_template_cpp, buffer);
    const std::string output_path = toUtf8Path(
        noise_image_directory / std::filesystem::u8path("output_dark_template.bmp"));
#ifdef _WIN32
    std::ofstream output_file(wafer_calib::stringToWstring(output_path), std::ios::binary);
#else
    std::ofstream output_file(output_path, std::ios::binary);
#endif
    if (!output_file.is_open()) {
        std::cerr << "[C++ 接口保存失败] " << output_path << std::endl;
        return 4;
    }
    output_file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    if (!output_file.good()) {
        std::cerr << "[C++ 接口保存失败] " << output_path << std::endl;
        return 4;
    }
    std::cout << "-> 成功保存暗场背景模板图至: " << output_path << std::endl;

    std::vector<const char*> c_paths;
    c_paths.reserve(dark_files.size());
    for (const auto& path : dark_files) {
        c_paths.push_back(path.c_str());
    }

    const std::string c_api_output_path = toUtf8Path(
        noise_image_directory / std::filesystem::u8path("output_dark_template_c_api.bmp"));
    const int c_result = Wafer_CreateDarkFrameTemplateFromFiles(
        c_paths.data(), static_cast<int>(c_paths.size()), c_api_output_path.c_str(), 1);
    if (c_result != WAFER_SUCCESS) {
        std::cerr << "[C-API 接口调用失败] 错误码: " << c_result << std::endl;
        return 5;
    }

    std::cout << "\n[C-API (C#导出) 接口调用成功] 已生成: " << c_api_output_path << std::endl;
    return 0;
}
