#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
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

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    std::cout << "========================================" << std::endl;
    std::cout << "开始运行 CircleCenterOffset 自动化单元测试" << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. 测试异常参数输入
    std::cout << "[Test 1] 校验非法输入参数..." << std::endl;
    {
        double dx = 0.0;
        double dy = 0.0;
        unsigned char dummy[100] = {0};

        // 空图像指针
        assert(Wafer_FindCircleCenterOffset(nullptr, 100, 100, &dx, &dy, dummy) == WAFER_ERR_INVALID_PARAM);
        // 无效宽高
        assert(Wafer_FindCircleCenterOffset(dummy, 0, 100, &dx, &dy, dummy) == WAFER_ERR_INVALID_PARAM);
        assert(Wafer_FindCircleCenterOffset(dummy, 100, -1, &dx, &dy, dummy) == WAFER_ERR_INVALID_PARAM);
        // 空输出指针
        assert(Wafer_FindCircleCenterOffset(dummy, 100, 100, nullptr, &dy, dummy) == WAFER_ERR_INVALID_PARAM);
        assert(Wafer_FindCircleCenterOffset(dummy, 100, 100, &dx, nullptr, dummy) == WAFER_ERR_INVALID_PARAM);
        assert(Wafer_FindCircleCenterOffset(dummy, 100, 100, &dx, &dy, nullptr) == WAFER_ERR_INVALID_PARAM);

        std::cout << "  -> 非法参数测试全部通过！" << std::endl;
    }

    // 2. 批量测试真实数据集中的 48 张图像
    std::cout << "\n[Test 2] 批量测试真实图像数据集 (20260822_0)..." << std::endl;
    const std::filesystem::path test_dir = findTestImagesDirectory();
    if (test_dir.empty()) {
        std::cerr << "错误：未找到测试图像目录！" << std::endl;
        return 1;
    }

    std::vector<std::filesystem::path> image_files;
    for (const auto& entry : std::filesystem::directory_iterator(test_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bmp" &&
            entry.path().filename().string().find("output") == std::string::npos) {
            image_files.push_back(entry.path());
        }
    }
    std::sort(image_files.begin(), image_files.end());

    std::cout << "找到 " << image_files.size() << " 张测试图像。" << std::endl;
    assert(image_files.size() >= 40); // 确保读到了完整批次

    int pass_count = 0;
    std::vector<double> elapsed_times_ms;

    for (size_t i = 0; i < image_files.size(); ++i) {
        const auto& path = image_files[i];
        const cv::Mat img = wafer_calib::readImageUnicode(toUtf8Path(path), cv::IMREAD_GRAYSCALE);
        assert(!img.empty() && img.type() == CV_8UC1);

        double dx = 0.0;
        double dy = 0.0;
        std::vector<unsigned char> result_bgr(static_cast<size_t>(img.total()) * 3U);

        const auto t_start = std::chrono::high_resolution_clock::now();
        const int ret = Wafer_FindCircleCenterOffset(
            img.data,
            img.cols,
            img.rows,
            &dx,
            &dy,
            result_bgr.data()
        );
        const auto t_end = std::chrono::high_resolution_clock::now();
        const double duration_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        elapsed_times_ms.push_back(duration_ms);

        if (ret == WAFER_SUCCESS) {
            ++pass_count;

            const double img_cx = img.cols / 2.0;
            const double img_cy = img.rows / 2.0;
            const double calc_cx = img_cx + dx;
            const double calc_cy = img_cy + dy;

            if (i % 8 == 0 || i == image_files.size() - 1) {
                std::cout << "  [" << i << "] " << path.filename().string()
                          << " -> dx=" << dx << ", dy=" << dy
                          << ", Center=(" << calc_cx << ", " << calc_cy << ")"
                          << ", 耗时=" << duration_ms << " ms" << std::endl;
            }
        } else {
            std::cerr << "  [" << i << "] " << path.filename().string() << " 失败，错误码=" << ret << std::endl;
        }
    }

    std::cout << "\n----------------------------------------" << std::endl;
    std::cout << "测试总结：" << std::endl;
    std::cout << "  成功率: " << pass_count << " / " << image_files.size()
              << " (" << (pass_count * 100.0 / image_files.size()) << "%)" << std::endl;

    assert(pass_count == static_cast<int>(image_files.size()));

    const double avg_time = std::accumulate(elapsed_times_ms.begin(), elapsed_times_ms.end(), 0.0) / elapsed_times_ms.size();

    std::cout << "  平均处理耗时: " << avg_time << " ms / 帧" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    std::cout << "\n[PASS] CircleCenterOffset 全部测试顺利通过！" << std::endl;
    return 0;
}
