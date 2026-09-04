#ifndef WAFER_CALIB_CORE_TYPES_HPP_
#define WAFER_CALIB_CORE_TYPES_HPP_

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace wafer_calib {

// 内部字符串转宽字符路径函数 (兼容 UTF-8 与 GBK/ANSI 双编码)
inline std::wstring stringToWstring(const std::string& str) {
    if (str.empty()) return L"";
#ifdef _WIN32
    // 1. 尝试以 UTF-8 格式转换
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), -1, NULL, 0);
    if (wlen > 0) {
        std::wstring wstr(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], wlen);
        wstr.resize(wlen - 1);
        return wstr;
    }
    // 2. 若非 UTF-8，按系统 ANSI (GBK/CP936) 格式转换
    wlen = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
    if (wlen > 0) {
        std::wstring wstr(wlen, 0);
        MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wstr[0], wlen);
        wstr.resize(wlen - 1);
        return wstr;
    }
#endif
    return std::wstring(str.begin(), str.end());
}

// 支持中文/Unicode路径的安全图像读取函数 (不依赖容易报非法字符异常的 std::filesystem::u8path)
inline cv::Mat readImageUnicode(const std::string& filepath, int flags = cv::IMREAD_COLOR) {
    if (filepath.empty()) return cv::Mat();

#ifdef _WIN32
    std::wstring wpath = stringToWstring(filepath);
    std::ifstream file(wpath, std::ios::binary | std::ios::ate);
#else
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
#endif

    if (!file.is_open()) {
        return cv::Mat();
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        return cv::Mat();
    }

    std::vector<uchar> ubuf(buffer.begin(), buffer.end());
    return cv::imdecode(ubuf, flags);
}

// 支持中文/Unicode路径的安全图像写入函数
inline bool writeImageUnicode(const std::string& filepath, const cv::Mat& image) {
    if (filepath.empty() || image.empty()) return false;
    std::string ext = ".bmp";
    size_t dot_pos = filepath.find_last_of('.');
    if (dot_pos != std::string::npos) {
        ext = filepath.substr(dot_pos);
    }
    std::vector<uchar> buf;
    if (!cv::imencode(ext, image, buf)) return false;
#ifdef _WIN32
    std::wstring wpath = stringToWstring(filepath);
    std::ofstream file(wpath, std::ios::binary);
#else
    std::ofstream file(filepath, std::ios::binary);
#endif
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    return true;
}

// 亚像素 2D 点
struct Point2D {
    double x = 0.0;
    double y = 0.0;

    Point2D() = default;
    Point2D(double px, double py) : x(px), y(py) {}
    Point2D(const cv::Point2f& pt) : x(pt.x), y(pt.y) {}
    Point2D(const cv::Point2d& pt) : x(pt.x), y(pt.y) {}

    cv::Point2d toCvPoint() const {
        return cv::Point2d(x, y);
    }
};

} // namespace wafer_calib

#endif // WAFER_CALIB_CORE_TYPES_HPP_
