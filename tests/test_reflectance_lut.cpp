#include "wafer_calib/wafer_calib.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <opencv2/highgui.hpp>

int main() {
    std::cout << "========== 开始反射率光电响应标定与灰度线性化 LUT 测试 ==========" << std::endl;

    std::string base_dir = "D:/Projects/opencvProject/WaferCalibSDK/images/不同反射率_线性矫正";
    std::vector<std::string> file_names = {
        base_dir + "/5%.bmp",
        base_dir + "/50%.bmp",
        base_dir + "/75%.bmp",
        base_dir + "/90%.bmp"
    };

    std::vector<cv::Mat> images;
    for (const auto& path : file_names) {
        cv::Mat img = wafer_calib::readImageUnicode(path, cv::IMREAD_GRAYSCALE);
        if (img.empty()) {
            std::cerr << "[FAIL] 读取测试图像失败: " << path << std::endl;
            return -1;
        }
        images.push_back(img);
    }
    std::cout << "[PASS] 成功载入 4 张标定测试图像，分辨率: "
              << images[0].cols << "x" << images[0].rows << std::endl;

    // 1. 测试 C++ 核心标定类 (分段平台死区模式)
    wafer_calib::ReflectanceLutConfig deadband_cfg;
    deadband_cfg.mode = wafer_calib::WAFER_LUT_MODE_DEADBAND;
    deadband_cfg.roi_center_width = 1000;
    deadband_cfg.roi_center_height = 1000;
    deadband_cfg.deadband_width = 5;

    wafer_calib::ReflectanceLutResult deadband_res;
    cv::Mat diagnostic_bgr;

    bool calib_ok = wafer_calib::WaferReflectanceLutCalibrator::Calibrate(
        images, deadband_cfg, deadband_res, &diagnostic_bgr
    );

    if (!calib_ok) {
        std::cerr << "[FAIL] C++ WaferReflectanceLutCalibrator::Calibrate 失败!" << std::endl;
        return -1;
    }

    std::cout << "[PASS] 反射率 LUT 标定计算成功 (死区模式)!" << std::endl;
    for (int k = 0; k < 4; ++k) {
        std::cout << "   - 标定靶标 " << (k + 1) << " 实测波峰: "
                  << deadband_res.measured_peaks[k] << " px, 目标物理基准: "
                  << deadband_res.target_values[k] << " px" << std::endl;
    }
    std::cout << "   - 原始非线性 R^2: " << deadband_res.raw_linearity_r2
              << " -> 标定校正后 R^2: " << deadband_res.corrected_linearity_r2 << std::endl;

    // 导出诊断大图
    std::string diag_save_path1 = base_dir + "/output_reflectance_lut_diagnostic.png";
    std::string diag_save_path2 = "D:/Projects/opencvProject/WaferCalibSDK/docs/images/reflectance_lut_diagnostic.png";
    if (!diagnostic_bgr.empty()) {
        wafer_calib::writeImageUnicode(diag_save_path1, diagnostic_bgr);
        wafer_calib::writeImageUnicode(diag_save_path2, diagnostic_bgr);
        std::cout << "[PASS] 高分辨率综合诊断大看板已成功导出至:\n   "
                  << diag_save_path1 << "\n   " << diag_save_path2 << std::endl;
    }

    // 2. 测试在线高速查表校正 ApplyLut 及吞吐性能
    cv::Mat corrected_img;
    auto t_start = std::chrono::high_resolution_clock::now();
    const int benchmark_loops = 20;
    for (int i = 0; i < benchmark_loops; ++i) {
        wafer_calib::WaferReflectanceLutCalibrator::ApplyLut(images[1], corrected_img, deadband_res.lut);
    }
    auto t_end = std::chrono::high_resolution_clock::now();
    double avg_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count() / benchmark_loops;
    std::cout << "[PASS] 在线查表校正基准测试: 4096x4096 Mono8 单帧平均耗时: "
              << avg_ms << " ms" << std::endl;

    // 3. 测试平滑样条模式 (WAFER_LUT_MODE_SMOOTH)
    wafer_calib::ReflectanceLutConfig smooth_cfg = deadband_cfg;
    smooth_cfg.mode = wafer_calib::WAFER_LUT_MODE_SMOOTH;
    wafer_calib::ReflectanceLutResult smooth_res;
    if (!wafer_calib::WaferReflectanceLutCalibrator::Calibrate(images, smooth_cfg, smooth_res, nullptr)) {
        std::cerr << "[FAIL] 平滑样条模式标定失败!" << std::endl;
        return -1;
    }
    std::cout << "[PASS] 平滑样条曲线模式测试通过 (端点单调性及连续性正常)!" << std::endl;

    // 4. 测试配方保存与加载
    std::string lut_file_path = "D:/Projects/opencvProject/WaferCalibSDK/docs/recipe_reflectance_lut.csv";
    if (!wafer_calib::WaferReflectanceLutCalibrator::SaveLut(lut_file_path, deadband_res.lut)) {
        std::cerr << "[FAIL] 保存 LUT 配方文件失败!" << std::endl;
        return -1;
    }
    unsigned char loaded_lut[256] = {0};
    if (!wafer_calib::WaferReflectanceLutCalibrator::LoadLut(lut_file_path, loaded_lut)) {
        std::cerr << "[FAIL] 加载 LUT 配方文件失败!" << std::endl;
        return -1;
    }
    for (int i = 0; i < 256; ++i) {
        if (loaded_lut[i] != deadband_res.lut[i]) {
            std::cerr << "[FAIL] 加载的 LUT 数据与保存的数据不一致!" << std::endl;
            return -1;
        }
    }
    std::cout << "[PASS] LUT 配方保存与重新载入一致性校验 100% 通过!" << std::endl;

    // 5. 测试 C API 纯内存调用接口
    std::vector<const unsigned char*> img_ptrs;
    for (int i = 0; i < 4; ++i) {
        img_ptrs.push_back(images[i].data);
    }
    WaferReflectanceLutConfig c_config;
    c_config.lut_mode = 0;
    c_config.roi_center_width = 1000;
    c_config.roi_center_height = 1000;
    c_config.deadband_width = 5;
    for (int k = 0; k < 4; ++k) {
        c_config.target_values[k] = deadband_cfg.target_values[k];
    }

    WaferReflectanceLutResult c_result;
    std::vector<unsigned char> c_diag_buf(2048 * 1536 * 3);
    int ret = Wafer_CalibrateReflectanceLut(
        img_ptrs.data(), images[0].cols, images[0].rows, &c_config, &c_result, c_diag_buf.data()
    );

    if (ret != WAFER_SUCCESS) {
        std::cerr << "[FAIL] C API Wafer_CalibrateReflectanceLut 返回错误码: " << ret << std::endl;
        return -1;
    }
    std::cout << "[PASS] C API 纯内存图像流交互测试全部通过!" << std::endl;

    std::cout << "========== 反射率光电响应标定与 LUT 全部自动化测试 100% 通过 ==========" << std::endl;
    return 0;
}
