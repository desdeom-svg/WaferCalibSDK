#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "wafer_calib/wafer_calib.hpp"

int main() {
    std::cout << "====================================================\n";
    std::cout << "  9点标定 (AxisPixelCalibration) 极简 3 接口测试\n";
    std::cout << "====================================================\n";

    const std::string img_dir = "D:/images/谷神星/标准化/AxisPixelCalibration";
    const std::string out_calib_json = "D:/images/谷神星/标准化/AxisPixelCalibration/AxisPixelCalib_Obj0.json";
    const std::string out_diag_img = "D:/images/谷神星/标准化/AxisPixelCalibration/9点标定综合诊断大图.bmp";
    const std::string sdk_out_diag = "D:/Projects/opencvProject/WaferCalibSDK/images/9点标定综合诊断大图.bmp";

    // 1. 读取 9 张 Mono8 图像 (Mark_0 ~ Mark_8)
    std::vector<cv::Mat> images(9);
    std::vector<const unsigned char*> buffers(9);

    int img_w = 0;
    int img_h = 0;

    for (int i = 0; i < 9; ++i) {
        std::string fn = img_dir + "/Mark_" + std::to_string(i) + ".bmp";
        images[i] = wafer_calib::readImageUnicode(fn, cv::IMREAD_GRAYSCALE);
        if (images[i].empty()) {
            std::cerr << "错误: 无法读取图像: " << fn << std::endl;
            return 1;
        }
        if (i == 0) {
            img_w = images[i].cols;
            img_h = images[i].rows;
        } else {
            if (images[i].cols != img_w || images[i].rows != img_h) {
                std::cerr << "错误: 图像尺寸不一致: " << fn << std::endl;
                return 1;
            }
        }
        buffers[i] = images[i].data;
    }

    std::cout << "成功载入 9 张标定图像，尺寸: " << img_w << " x " << img_h << std::endl;

    const double initial_axis_x = 0.02905;
    const double initial_axis_y = 83.58272;
    const double step_size_mm = 1.65;
    const int diag_w = 3000;
    const int diag_h = 2400;
    std::vector<unsigned char> diag_buf(diag_w * diag_h * 3);

    double pixel_scale_x_um = 0.0;
    double pixel_scale_y_um = 0.0;

    // 2. 调用 C API 主标定接口 (极简 1: Wafer_CalibrateAxisPixelGrid)
    std::cout << "\n1. 开始执行 Wafer_CalibrateAxisPixelGrid 标定计算..." << std::endl;
    auto t_calib_start = std::chrono::high_resolution_clock::now();

    int status = -999;
    try {
        status = Wafer_CalibrateAxisPixelGrid(
            buffers.data(),
            9,
            img_w,
            img_h,
            initial_axis_x,
            initial_axis_y,
            step_size_mm,
            out_calib_json.c_str(),
            &pixel_scale_x_um,
            &pixel_scale_y_um,
            diag_buf.data(),
            diag_w,
            diag_h
        );
    } catch (const std::exception& e) {
        std::cerr << "捕获到 C++ 异常: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "捕获到未知异常!" << std::endl;
        return 1;
    }

    auto t_calib_end = std::chrono::high_resolution_clock::now();
    double calib_ms = std::chrono::duration<double, std::milli>(t_calib_end - t_calib_start).count();

    if (status != WAFER_SUCCESS) {
        std::cerr << "标定失败，错误码: " << status << std::endl;
        return 1;
    }
    std::cout << "标定计算成功! 耗时: " << std::fixed << std::setprecision(2) << calib_ms << " ms" << std::endl;
    std::cout << "直接输出像元物理当量: X = " << std::setprecision(4) << pixel_scale_x_um
              << " um/px, Y = " << pixel_scale_y_um << " um/px\n";
    std::cout << "已自动输出标定配方 JSON 至: " << out_calib_json << std::endl;

    // 输出诊断大图
    cv::Mat diag_mat(diag_h, diag_w, CV_8UC3, diag_buf.data());
    if (wafer_calib::writeImageUnicode(out_diag_img, diag_mat)) {
        std::cout << "成功保存诊断大图至: " << out_diag_img << std::endl;
    }
    if (wafer_calib::writeImageUnicode(sdk_out_diag, diag_mat)) {
        std::cout << "成功同步诊断大图至: " << sdk_out_diag << std::endl;
    }

    // 3. 验证极简在线坐标正反变换 (极简 2 & 3: Wafer_TransformPixelToAxis / Wafer_TransformAxisToPixel)
    std::cout << "\n2. 验证在线坐标正反转换与往返一致性..." << std::endl;

    // 测试图像中心点转换
    double test_u = 2048.0;
    double test_v = 2048.0;
    double out_axis_x = 0.0;
    double out_axis_y = 0.0;

    status = Wafer_TransformPixelToAxis(out_calib_json.c_str(), test_u, test_v, &out_axis_x, &out_axis_y);
    if (status != WAFER_SUCCESS) {
        std::cerr << "Wafer_TransformPixelToAxis 失败，错误码: " << status << std::endl;
        return 1;
    }

    double roundtrip_u = 0.0;
    double roundtrip_v = 0.0;
    status = Wafer_TransformAxisToPixel(out_calib_json.c_str(), out_axis_x, out_axis_y, &roundtrip_u, &roundtrip_v);
    if (status != WAFER_SUCCESS) {
        std::cerr << "Wafer_TransformAxisToPixel 失败，错误码: " << status << std::endl;
        return 1;
    }

    double err_dist = std::hypot(roundtrip_u - test_u, roundtrip_v - test_v);
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "输入像面像素: (" << test_u << ", " << test_v << ")\n";
    std::cout << "转换机械轴坐标: (" << out_axis_x << ", " << out_axis_y << ") mm\n";
    std::cout << "逆变换还原像素: (" << roundtrip_u << ", " << roundtrip_v << ")\n";
    std::cout << "往返重合误差:   " << err_dist << " px (满足 < 1e-4 精度指标)\n";

    if (err_dist > 1e-4) {
        std::cerr << "错误: 往返重合误差超标!" << std::endl;
        return 1;
    }

    // 4. 压测透明内存缓存性能 (10,000 次连续转换)
    std::cout << "\n3. 压测透明内存缓存机制性能 (10,000 次转换)..." << std::endl;
    const int benchmark_count = 10000;
    auto t_bench_start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < benchmark_count; ++i) {
        double ax = 0.0, ay = 0.0;
        double u_val = 1000.0 + (i % 2000);
        double v_val = 1000.0 + ((i * 2) % 2000);
        Wafer_TransformPixelToAxis(out_calib_json.c_str(), u_val, v_val, &ax, &ay);
    }

    auto t_bench_end = std::chrono::high_resolution_clock::now();
    double total_ns = std::chrono::duration<double, std::nano>(t_bench_end - t_bench_start).count();
    double per_call_ns = total_ns / benchmark_count;

    std::cout << "完成 " << benchmark_count << " 次正变换，总耗时: "
              << total_ns / 1e6 << " ms, 单次调用平均耗时: "
              << per_call_ns << " ns (" << per_call_ns / 1000.0 << " us)\n";

    std::cout << "\n====================================================\n";
    std::cout << "  极简 3 接口全部功能与性能验证通过!\n";
    std::cout << "====================================================\n";
    return 0;
}

