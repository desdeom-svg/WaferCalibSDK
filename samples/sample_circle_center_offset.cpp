#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
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

std::filesystem::path findTestImagesDirectory() {
    std::filesystem::path directory = std::filesystem::absolute(std::filesystem::current_path());
    const std::filesystem::path relative_dir = std::filesystem::u8path("images/求圆中心与图像中心的差值/20260822_0");

    while (true) {
        const std::filesystem::path candidate = directory / relative_dir;
        if (std::filesystem::is_directory(candidate)) {
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

    std::cout << "================================================================" << std::endl;
    std::cout << "        圆中心与图像中心差值计算 - 一键批量处理程序              " << std::endl;
    std::cout << "================================================================" << std::endl;

    const std::filesystem::path test_dir = findTestImagesDirectory();
    if (test_dir.empty()) {
        std::cerr << "错误：未找到测试图像目录 images/求圆中心与图像中心的差值/20260822_0" << std::endl;
        return 1;
    }

    std::cout << "输入图像目录: " << toUtf8Path(test_dir) << std::endl;

    // 创建结果保存子目录
    const std::filesystem::path results_dir = test_dir / "results";
    std::error_code ec;
    std::filesystem::create_directories(results_dir, ec);
    std::cout << "标注图保存目录: " << toUtf8Path(results_dir) << std::endl;

    // 收集所有原始 .bmp 图片
    std::vector<std::filesystem::path> image_files;
    for (const auto& entry : std::filesystem::directory_iterator(test_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bmp") {
            const std::string filename = entry.path().filename().string();
            // 排除可能存在的已生成结果文件
            if (filename.find("output") == std::string::npos && filename.find("result") == std::string::npos) {
                image_files.push_back(entry.path());
            }
        }
    }
    std::sort(image_files.begin(), image_files.end());

    if (image_files.empty()) {
        std::cerr << "未在目录下找到任何原始 .bmp 图像！" << std::endl;
        return 2;
    }

    std::cout << "开始批量处理共 " << image_files.size() << " 张图像...\n" << std::endl;
    std::cout << std::left
              << std::setw(12) << "文件名"
              << std::setw(18) << "图像中心(X, Y)"
              << std::setw(22) << "圆中心(X, Y)"
              << std::setw(14) << "dx (像素)"
              << std::setw(14) << "dy (像素)"
              << std::setw(12) << "耗时(ms)"
              << "状态" << std::endl;
    std::cout << std::string(85, '-') << std::endl;

    int success_count = 0;
    double total_time_ms = 0.0;

    for (const auto& image_path : image_files) {
        const std::string filename = image_path.filename().string();
        const cv::Mat mono8 = wafer_calib::readImageUnicode(toUtf8Path(image_path), cv::IMREAD_GRAYSCALE);

        if (mono8.empty() || mono8.type() != CV_8UC1) {
            std::cout << std::left << std::setw(12) << filename << "读取失败 (非 Mono8 格式)" << std::endl;
            continue;
        }

        const int width = mono8.cols;
        const int height = mono8.rows;
        std::vector<unsigned char> result_bgr_buffer(static_cast<size_t>(width * height * 3));

        double offset_x = 0.0;
        double offset_y = 0.0;

        const auto start_time = std::chrono::high_resolution_clock::now();
        const int ret = Wafer_FindCircleCenterOffset(
            mono8.data,
            width,
            height,
            &offset_x,
            &offset_y,
            result_bgr_buffer.data()
        );
        const auto end_time = std::chrono::high_resolution_clock::now();
        const double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        total_time_ms += duration_ms;

        if (ret == WAFER_SUCCESS) {
            ++success_count;
            const double img_cx = width / 2.0;
            const double img_cy = height / 2.0;
            const double circle_cx = img_cx + offset_x;
            const double circle_cy = img_cy + offset_y;

            // 保存诊断标注图到 results 子目录
            const std::string stem = image_path.stem().string();
            const std::filesystem::path out_bmp_path = results_dir / (stem + "_result.bmp");
            const cv::Mat result_bgr(height, width, CV_8UC3, result_bgr_buffer.data());
            writeImageUnicode(out_bmp_path, result_bgr);

            std::cout << std::left
                      << std::setw(12) << filename
                      << std::setw(18) << (cv::format("(%.1f, %.1f)", img_cx, img_cy))
                      << std::setw(22) << (cv::format("(%.2f, %.2f)", circle_cx, circle_cy))
                      << std::setw(14) << (cv::format("%.3f", offset_x))
                      << std::setw(14) << (cv::format("%.3f", offset_y))
                      << std::setw(12) << (cv::format("%.1f", duration_ms))
                      << "成功" << std::endl;
        } else {
            std::cout << std::left
                      << std::setw(12) << filename
                      << "处理失败，错误码: " << ret << std::endl;
        }
    }

    std::cout << std::string(85, '-') << std::endl;
    std::cout << "处理完成！" << std::endl;
    std::cout << "成功率: " << success_count << " / " << image_files.size()
              << " (" << (success_count * 100.0 / image_files.size()) << "%)" << std::endl;
    std::cout << "平均耗时: " << (total_time_ms / image_files.size()) << " ms / 帧" << std::endl;
    std::cout << "所有诊断标注图已输出至: " << toUtf8Path(results_dir) << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
