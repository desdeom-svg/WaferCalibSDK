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

// 获取当前可执行程序所在的真实目录 (支持 Windows Unicode 路径)
std::filesystem::path getExecutableDirectory() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(NULL, &buffer[0], static_cast<DWORD>(buffer.size()));
    while (length >= buffer.size()) {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(NULL, &buffer[0], static_cast<DWORD>(buffer.size()));
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

std::string toUtf8Path(const std::filesystem::path& path) {
    return path.u8string();
}

// 支持 Unicode 宽字符路径的安全 PNG 图像写入函数
bool writePngImageUnicode(const std::filesystem::path& output_path, const cv::Mat& image) {
    std::vector<uchar> encoded;
    std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 3}; // 平衡压缩速度与文件大小
    if (!cv::imencode(".png", image, encoded, params)) {
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

// 检查文件扩展名是否为 .bmp (不区分大小写)
bool isBmpFile(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(c));
    }
    return ext == ".bmp";
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(65001); // 设置控制台输出编码为 UTF-8，防止中文乱码
#endif

    std::cout << "================================================================" << std::endl;
    std::cout << "        WaferCalibSDK - 圆中心与图像中心差值计算离线工具        " << std::endl;
    std::cout << "================================================================" << std::endl;

    // 1. 确定运行工作目录 (优先使用 exe 所在同级目录，支持传入命令行参数)
    std::filesystem::path work_dir = getExecutableDirectory();
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
        work_dir = std::filesystem::u8path(argv[1]);
    }

    std::cout << "程序工作目录: " << toUtf8Path(work_dir) << std::endl;

    // 2. 在 exe 同级目录下创建 result 结果主文件夹
    const std::filesystem::path base_result_dir = work_dir / "result";
    std::error_code ec;
    std::filesystem::create_directories(base_result_dir, ec);
    std::cout << "PNG 结果输出目录: " << toUtf8Path(base_result_dir) << std::endl;

    // 3. 递归扫描同级目录及所有子文件夹下的 .bmp 图像
    std::vector<std::filesystem::path> bmp_files;
    if (std::filesystem::exists(work_dir) && std::filesystem::is_directory(work_dir)) {
        for (std::filesystem::recursive_directory_iterator iter(work_dir), end; iter != end; ++iter) {
            const auto& entry = *iter;
            const std::string name = entry.path().filename().string();

            // 如果是目录且为 result/results 文件夹，则跳过该目录内部递归
            if (entry.is_directory()) {
                if (name == "result" || name == "results" || name == ".git") {
                    iter.disable_recursion_pending();
                }
                continue;
            }

            // 过滤匹配 .bmp 文件
            if (entry.is_regular_file() && isBmpFile(entry.path())) {
                if (name.find("_result") == std::string::npos && name.find("output") == std::string::npos) {
                    bmp_files.push_back(entry.path());
                }
            }
        }
    }
    std::sort(bmp_files.begin(), bmp_files.end());

    if (bmp_files.empty()) {
        std::cout << "\n[提示] 未在同级目录及子文件夹中找到待检测的 .bmp 图像！" << std::endl;
        std::cout << "请将待检测的 .bmp 图像放置于本程序同级目录或子文件夹后重新运行。" << std::endl;
        std::cout << "================================================================" << std::endl;
        return 0;
    }

    std::cout << "\n共找到 " << bmp_files.size() << " 张 .bmp 待处理图像，开始批量分析...\n" << std::endl;

    // 4. 打印表头
    std::cout << std::left
              << std::setw(6)  << "序号"
              << std::setw(28) << "相对路径/文件名"
              << std::setw(18) << "图像中心(X, Y)"
              << std::setw(22) << "圆中心(X, Y)"
              << std::setw(14) << "dx (像素)"
              << std::setw(14) << "dy (像素)"
              << std::setw(12) << "耗时(ms)"
              << "状态" << std::endl;
    std::cout << std::string(105, '-') << std::endl;

    int success_count = 0;
    double total_time_ms = 0.0;

    // 5. 逐张处理并保存 PNG 格式标注图
    for (size_t i = 0; i < bmp_files.size(); ++i) {
        const auto& file_path = bmp_files[i];
        
        // 计算相对路径
        std::string rel_path_str;
        try {
            rel_path_str = std::filesystem::relative(file_path, work_dir).u8string();
        } catch (...) {
            rel_path_str = file_path.filename().u8string();
        }

        const cv::Mat mono8 = wafer_calib::readImageUnicode(toUtf8Path(file_path), cv::IMREAD_GRAYSCALE);

        if (mono8.empty() || mono8.type() != CV_8UC1) {
            std::cout << std::left
                      << std::setw(6)  << (i + 1)
                      << std::setw(28) << rel_path_str
                      << "读取失败 (非 Mono8 灰度图像)" << std::endl;
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

            // 计算输出的 PNG 目标路径 (在 result 目录下保持相对子目录结构)
            std::filesystem::path rel_file = std::filesystem::relative(file_path, work_dir);
            std::filesystem::path out_sub_dir = base_result_dir / rel_file.parent_path();
            std::filesystem::create_directories(out_sub_dir, ec);

            const std::string stem = file_path.stem().string();
            const std::filesystem::path out_png_path = out_sub_dir / (stem + "_result.png");
            const cv::Mat result_bgr(height, width, CV_8UC3, result_bgr_buffer.data());
            writePngImageUnicode(out_png_path, result_bgr);

            std::cout << std::left
                      << std::setw(6)  << (i + 1)
                      << std::setw(28) << rel_path_str
                      << std::setw(18) << (cv::format("(%.1f, %.1f)", img_cx, img_cy))
                      << std::setw(22) << (cv::format("(%.2f, %.2f)", circle_cx, circle_cy))
                      << std::setw(14) << (cv::format("%.3f", offset_x))
                      << std::setw(14) << (cv::format("%.3f", offset_y))
                      << std::setw(12) << (cv::format("%.1f", duration_ms))
                      << "成功" << std::endl;
        } else {
            std::cout << std::left
                      << std::setw(6)  << (i + 1)
                      << std::setw(28) << rel_path_str
                      << "处理失败，错误码: " << ret << std::endl;
        }
    }

    // 6. 汇总报告
    std::cout << std::string(105, '-') << std::endl;
    std::cout << "处理完成！" << std::endl;
    std::cout << "  - 成功处理: " << success_count << " / " << bmp_files.size()
              << " (" << (success_count * 100.0 / bmp_files.size()) << "%)" << std::endl;
    std::cout << "  - 平均耗时: " << (total_time_ms / bmp_files.size()) << " ms / 帧" << std::endl;
    std::cout << "  - PNG 结果图已全部保存至: " << toUtf8Path(base_result_dir) << std::endl;
    std::cout << "================================================================" << std::endl;

    return 0;
}
