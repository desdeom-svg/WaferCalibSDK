#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "wafer_calib/c_api/wafer_calib_c.h"
#include "wafer_calib/modules/line_angle.hpp"

#include <filesystem>

namespace {

cv::Mat readImageUnicode(const std::string& path) {
    std::filesystem::path p = std::filesystem::u8path(path);
    std::ifstream file(p, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return cv::Mat();
    }
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        return cv::Mat();
    }
    return cv::imdecode(cv::Mat(1, static_cast<int>(size), CV_8UC1, buffer.data()), cv::IMREAD_GRAYSCALE);
}

bool writeImageUnicode(const std::string& path, const cv::Mat& image) {
    std::vector<unsigned char> buf;
    if (!cv::imencode(".jpg", image, buf)) {
        return false;
    }
    std::filesystem::path p = std::filesystem::u8path(path);
    std::ofstream file(p, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    return true;
}

} // namespace

int main() {
    std::cout << "=====================================================\n";
    std::cout << " [WaferCalibSDK] 双视野大基线标定线全局测角测试启动\n";
    std::cout << "=====================================================\n";

    // 1. 合成图像基础测试 (Synthetic Verification)
    {
        std::cout << "\n[Test 1] 合成双视野图像标称测试...\n";
        const int w = 2000;
        const int h = 1000;
        cv::Mat view1(h, w, CV_8UC1, cv::Scalar(220));
        cv::Mat view2(h, w, CV_8UC1, cv::Scalar(220));

        // 设定直线世界坐标几何：
        // 设物理 X 位移 delta_x = 100.0 mm
        // 像元垂直当量 sy = 1.0 um/px = 0.001 mm/px
        // 设物理直线倾角 theta = 0.5 度
        // 则物理落差 delta_y = delta_x * tan(0.5 deg) = 100 * tan(0.5 * pi / 180) = 0.872686 mm
        // 像面落差 delta_v = -delta_y / 0.001 = -872.686 像素
        // 设 view1 中心高度 v1 = 900.0 px, view2 中心高度 v2 = 900.0 - 872.686 = 27.314 px
        const double true_angle_deg = 0.5;
        const double delta_x_mm = 100.0;
        const double sy_um = 1.0;
        const double rad = true_angle_deg * CV_PI / 180.0;
        const double dy_mm = delta_x_mm * std::tan(rad);
        const double dv_px = -dy_mm / (sy_um / 1000.0);

        const double v1_center = 900.0;
        const double v2_center = v1_center + dv_px;

        // 在 view1 中画出中心过 (w/2, v1_center) 角度为 0.5 度的直线
        const cv::Point p1_start(0, static_cast<int>(std::round(v1_center - (w / 2.0) * std::tan(rad))));
        const cv::Point p1_end(w, static_cast<int>(std::round(v1_center + (w / 2.0) * std::tan(rad))));
        cv::line(view1, p1_start, p1_end, cv::Scalar(20), 18, cv::LINE_8);

        // 在 view2 中画出中心过 (w/2, v2_center) 角度为 0.5 度的直线
        const cv::Point p2_start(0, static_cast<int>(std::round(v2_center - (w / 2.0) * std::tan(rad))));
        const cv::Point p2_end(w, static_cast<int>(std::round(v2_center + (w / 2.0) * std::tan(rad))));
        cv::line(view2, p2_start, p2_end, cv::Scalar(20), 18, cv::LINE_8);

        wafer_calib::TwoViewLineAngleResult res;
        cv::Mat diag;
        const wafer_calib::Status st = wafer_calib::LineAngleModule::findTwoViewHorizontalLineAngle(
            view1, view2, delta_x_mm, sy_um, res, diag, 1600, 900);

        if (!st.ok()) {
            std::cerr << "合成测试失败: " << st.message << std::endl;
            return 1;
        }

        std::cout << "合成图像解算结果:\n";
        std::cout << "  - 全局角度: " << res.global_angle_deg << " 度 (标称: 0.5000 度)\n";
        std::cout << "  - 视野 1 局部角度: " << res.view1_angle_deg << " 度\n";
        std::cout << "  - 视野 2 局部角度: " << res.view2_angle_deg << " 度\n";
        std::cout << "  - 像面落差 dv: " << res.delta_v_pixels << " px (理论: " << dv_px << " px)\n";

        if (std::abs(res.global_angle_deg - true_angle_deg) > 0.05) {
            std::cerr << "合成测试误差过大！\n";
            return 2;
        }
    }

    // 2. 实测机台图像测试 (Real Field Images: 角度调整1.bmp & 角度调整2.bmp)
    {
        std::cout << "\n[Test 2] 实测双视野图像标定检测 (4096 x 4096)...\n";
        const std::string img1_path = "images/根据线输出角度/角度调整1.bmp";
        const std::string img2_path = "images/根据线输出角度/角度调整2.bmp";

        cv::Mat img1 = readImageUnicode(img1_path);
        cv::Mat img2 = readImageUnicode(img2_path);

        if (img1.empty() || img2.empty()) {
            std::cerr << "警告: 无法读取实测图像路径: " << img1_path << " 或 " << img2_path << "\n";
            return 3;
        }

        std::cout << "成功载入实测图像: View1(" << img1.cols << "x" << img1.rows << "), View2("
                  << img2.cols << "x" << img2.rows << ")\n";

        // 设定实际物理工况参数：
        // 设机台从左端视野 1 平移到右端视野 2 机械轴移动距离 stage_delta_x = 285.000 mm
        // 9 点标定实测像元垂直当量: pixel_scale_y = 0.9009 um/px
        const double stage_delta_x_mm = 285.000;
        const double pixel_scale_y_um = 0.9009;

        // C++ 接口测试并生成 3000 x 1800 综合诊断看板大图
        wafer_calib::TwoViewLineAngleResult result;
        cv::Mat diagnostic_3000x1800;

        const wafer_calib::Status st = wafer_calib::LineAngleModule::findTwoViewHorizontalLineAngle(
            img1, img2, stage_delta_x_mm, pixel_scale_y_um, result, diagnostic_3000x1800, 3000, 1800);

        if (!st.ok()) {
            std::cerr << "实测图像 C++ 解算失败: " << st.message << std::endl;
            return 4;
        }

        std::cout << "\n>>> 实测图像双视野大基线解算结果 <<<\n";
        std::cout << "  - 全局大基线角度: " << result.global_angle_deg << " 度 ("
                  << result.global_angle_arcmin << " 角分)\n";
        std::cout << "  - 视野 1 局部拟合角度: " << result.view1_angle_deg << " 度\n";
        std::cout << "  - 视野 2 局部拟合角度: " << result.view2_angle_deg << " 度\n";
        std::cout << "  - 视野 1 中心切线高度: " << result.view1_center_y << " px\n";
        std::cout << "  - 视野 2 中心切线高度: " << result.view2_center_y << " px\n";
        std::cout << "  - 像面垂直像素落差: " << result.delta_v_pixels << " px\n";
        std::cout << "  - 空间物理高度差: " << result.delta_y_world_mm << " mm\n";
        std::cout << "  - 诊断看板图尺寸: " << diagnostic_3000x1800.cols << " x " << diagnostic_3000x1800.rows << "\n";

        // 保存 3000 x 1800 诊断图至本地检验
        const std::string out_diag_path = "images/根据线输出角度/two_view_line_angle_diag_3000x1800.jpg";
        if (writeImageUnicode(out_diag_path, diagnostic_3000x1800)) {
            std::cout << "成功保存 3000x1800 工业综合诊断看板图至: " << out_diag_path << "\n";
        }

        // 3. C API 纯平铺导出接口验证
        std::cout << "\n[Test 3] C API 纯平铺函数 (Wafer_FindTwoViewHorizontalLineAngle) 验证...\n";
        double c_global_angle = 0.0;
        double c_v1_angle = 0.0;
        double c_v2_angle = 0.0;
        std::vector<unsigned char> c_diag_buf(3000 * 1800 * 3);

        const int c_status = Wafer_FindTwoViewHorizontalLineAngle(
            img1.data,
            img2.data,
            img1.cols,
            img1.rows,
            stage_delta_x_mm,
            pixel_scale_y_um,
            &c_global_angle,
            &c_v1_angle,
            &c_v2_angle,
            c_diag_buf.data(),
            3000,
            1800);

        if (c_status != WAFER_SUCCESS) {
            std::cerr << "C API 调用失败，错误码: " << c_status << "\n";
            return 5;
        }

        std::cout << "C API 结果一致性校验通过:\n";
        std::cout << "  - C API 全局角度: " << c_global_angle << " 度\n";
        std::cout << "  - C API 视野1角度: " << c_v1_angle << " 度\n";
        std::cout << "  - C API 视野2角度: " << c_v2_angle << " 度\n";

        if (std::abs(c_global_angle - result.global_angle_deg) > 1e-6) {
            std::cerr << "C API 与 C++ 计算结果不一致！\n";
            return 6;
        }

        // 4. 非法入参鲁棒性校验
        std::cout << "\n[Test 4] 非法参数健壮性校验...\n";
        // 机械位移为 0
        if (Wafer_FindTwoViewHorizontalLineAngle(img1.data, img2.data, img1.cols, img1.rows, 0.0, pixel_scale_y_um, &c_global_angle, nullptr, nullptr, nullptr, 0, 0) != WAFER_ERR_INVALID_PARAM) {
            std::cerr << "零位移防御失效！\n";
            return 7;
        }
        // 像元当量为 0
        if (Wafer_FindTwoViewHorizontalLineAngle(img1.data, img2.data, img1.cols, img1.rows, stage_delta_x_mm, 0.0, &c_global_angle, nullptr, nullptr, nullptr, 0, 0) != WAFER_ERR_INVALID_PARAM) {
            std::cerr << "零当量防御失效！\n";
            return 8;
        }
        // 空指针输入
        if (Wafer_FindTwoViewHorizontalLineAngle(nullptr, img2.data, img1.cols, img1.rows, stage_delta_x_mm, pixel_scale_y_um, &c_global_angle, nullptr, nullptr, nullptr, 0, 0) != WAFER_ERR_INVALID_PARAM) {
            std::cerr << "空指针防御失效！\n";
            return 9;
        }
        std::cout << "所有非法入参防御测试全部通过！\n";
    }

    std::cout << "\n=====================================================\n";
    std::cout << " [WaferCalibSDK] 所有双视野标定线测试全部 PASS 通过！\n";
    std::cout << "=====================================================\n";
    return 0;
}
