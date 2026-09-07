#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <filesystem>

#include <opencv2/opencv.hpp>

#include "wafer_calib/wafer_calib.hpp"

namespace {

bool runRotationCenterTest() {
    const std::string img_dir = "D:/images/谷神星/标准化/旋转一圈拟合圆求平台旋转中心";
    if (!std::filesystem::exists(wafer_calib::stringToWstring(img_dir))) {
        std::cerr << "[WARN] 测试图片目录不存在: " << img_dir << std::endl;
        return false;
    }

    const std::vector<std::string> file_names = {
        "Pos_0.bmp", "Pos_30.bmp", "Pos_60.bmp", "Pos_90.bmp", "Pos_120.bmp", "Pos_150.bmp",
        "Pos_180.bmp", "Pos_210.bmp", "Pos_240.bmp", "Pos_270.bmp", "Pos_300.bmp", "Pos_330.bmp"
    };

    std::vector<std::string> full_paths;
    std::vector<const char*> c_paths;
    full_paths.reserve(file_names.size());
    c_paths.reserve(file_names.size());

    for (const auto& fn : file_names) {
        full_paths.push_back(img_dir + "/" + fn);
        c_paths.push_back(full_paths.back().c_str());
    }

    std::cout << "========== 开始平台旋转中心拟合与大结果图生成测试 ==========" << std::endl;

    // 1. 测试从文件路径调用 C API 接口
    WaferPlatformRotationCenterResult result{};
    std::string diag_save_path = img_dir + "/output_rotation_center_diagnostic.png";

    int ret = Wafer_CalculatePlatformRotationCenterFromFiles(
        c_paths.data(),
        static_cast<int>(c_paths.size()),
        &result,
        diag_save_path.c_str()
    );

    if (ret != WAFER_SUCCESS) {
        std::cerr << "[FAIL] Wafer_CalculatePlatformRotationCenterFromFiles 失败，错误码: " << ret << std::endl;
        return false;
    }

    std::cout << "[PASS] 平台旋转中心计算成功!" << std::endl
              << "   - 有效识别点数: " << result.valid_point_count << " / " << c_paths.size() << std::endl
              << "   - 旋转中心 (Cx, Cy): (" << result.center_x << ", " << result.center_y << ") 像素" << std::endl
              << "   - 旋转偏心半径 (R): " << result.radius << " 像素" << std::endl
              << "   - 全局拟合 RMS 残差: " << result.rms_error << " 像素" << std::endl
              << "   - 最大单点径向偏差: " << result.max_error << " 像素" << std::endl;

    if (result.valid_point_count != 12) {
        std::cerr << "[FAIL] 识别点数不符合预期 (应为 12 点): " << result.valid_point_count << std::endl;
        return false;
    }

    if (std::abs(result.center_x - 2228.45) > 5.0 || std::abs(result.center_y - 1718.43) > 5.0) {
        std::cerr << "[FAIL] 旋转中心偏差过大: (" << result.center_x << ", " << result.center_y << ")" << std::endl;
        return false;
    }

    if (std::abs(result.radius - 1499.81) > 5.0) {
        std::cerr << "[FAIL] 旋转偏心半径偏差过大: " << result.radius << std::endl;
        return false;
    }

    if (result.rms_error > 3.0) {
        std::cerr << "[FAIL] RMS 残差过大: " << result.rms_error << std::endl;
        return false;
    }

    // 复制一份产物到 docs/images 供技术文档引用
    std::string docs_img_path = "D:/Projects/opencvProject/WaferCalibSDK/docs/images/rotation_center_diagnostic.png";
    if (std::filesystem::exists(wafer_calib::stringToWstring(diag_save_path))) {
        std::filesystem::copy_file(
            wafer_calib::stringToWstring(diag_save_path),
            wafer_calib::stringToWstring(docs_img_path),
            std::filesystem::copy_options::overwrite_existing
        );
        std::cout << "   - 诊断图已同步输出至文档目录: " << docs_img_path << std::endl;
    }

    // 2. 测试内存图像数组 C API 接口
    std::vector<cv::Mat> mono_imgs;
    std::vector<const unsigned char*> img_ptrs;
    mono_imgs.reserve(full_paths.size());
    img_ptrs.reserve(full_paths.size());

    for (const auto& fp : full_paths) {
        cv::Mat m = wafer_calib::readImageUnicode(fp, cv::IMREAD_GRAYSCALE);
        if (m.empty()) {
            std::cerr << "[FAIL] 读取图像失败: " << fp << std::endl;
            return false;
        }
        mono_imgs.push_back(m);
        img_ptrs.push_back(m.data);
    }

    const int w = mono_imgs[0].cols;
    const int h = mono_imgs[0].rows;
    std::vector<unsigned char> mem_diag_bgr(w * h * 3, 0);
    WaferPlatformRotationCenterResult mem_res{};

    int mem_ret = Wafer_CalculatePlatformRotationCenter(
        img_ptrs.data(),
        static_cast<int>(img_ptrs.size()),
        w,
        h,
        &mem_res,
        mem_diag_bgr.data()
    );

    if (mem_ret != WAFER_SUCCESS) {
        std::cerr << "[FAIL] Wafer_CalculatePlatformRotationCenter (内存版) 失败，错误码: " << mem_ret << std::endl;
        return false;
    }

    if (std::abs(mem_res.center_x - result.center_x) > 1e-4 ||
        std::abs(mem_res.center_y - result.center_y) > 1e-4) {
        std::cerr << "[FAIL] 内存版接口计算结果与文件版不一致!" << std::endl;
        return false;
    }

    std::cout << "[PASS] 内存图像数组版 C API 接口测试通过！" << std::endl;
    std::cout << "========== 平台旋转中心全部自动化测试 100% 通过 ==========" << std::endl;
    return true;
}

} // namespace

int main() {
    if (!runRotationCenterTest()) {
        return 1;
    }
    return 0;
}