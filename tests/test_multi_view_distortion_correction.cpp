#include <iostream>
#include <vector>
#include <string>
#include <cmath>

#include <opencv2/opencv.hpp>

#include "wafer_calib/c_api/wafer_calib_c.h"
#include "wafer_calib/modules/distortion_correction.hpp"

namespace {

struct DatasetConfig {
    std::string folder_name;
    double pitch_mm;
    double step_mm;
};

bool runRealDatasetTest(const std::string& base_dir) {
    const std::vector<DatasetConfig> configs = {
        {"1.5XImages", 1.0, 1.5},
        {"2.5XImages", 0.6, 0.9},
        {"5XImages", 0.3, 0.45},
        {"10XImages", 0.15, 0.22},
        {"20XImages", 0.07, 0.135}
    };

    const std::vector<std::string> view_files = {
        "中心.bmp", "左上.bmp", "左下.bmp", "右上.bmp", "右下.bmp"
    };

    std::cout << "========== 开始 5 视野多倍率真实图像标定测试 ==========" << std::endl;

    for (const auto& cfg : configs) {
        std::string folder_path = base_dir + "/" + cfg.folder_name;
        std::vector<std::string> full_paths;
        full_paths.reserve(5);
        for (const auto& vf : view_files) {
            full_paths.push_back(folder_path + "/" + vf);
        }

        const char* c_paths[5] = {
            full_paths[0].c_str(),
            full_paths[1].c_str(),
            full_paths[2].c_str(),
            full_paths[3].c_str(),
            full_paths[4].c_str()
        };

        WaferDotGridDistortionTemplate tmpl{};
        std::string diag_path = folder_path + "/output_diagnostic.png";

        int res = Wafer_CreateMultiViewDotGridTemplateFromFiles(
            c_paths,
            10,
            10,
            cfg.pitch_mm,
            cfg.step_mm,
            &tmpl,
            diag_path.c_str()
        );

        if (res != WAFER_SUCCESS) {
            std::cerr << "[FAIL] " << cfg.folder_name << " 标定失败，错误码: " << res << std::endl;
            return false;
        }

        std::cout << "[PASS] " << cfg.folder_name
                  << " 标定成功! 有效点数=" << tmpl.detected_point_count
                  << ", 全局RMS=" << tmpl.rms_error_pixels << " px" << std::endl;

        if (tmpl.detected_point_count < 450) {
            std::cerr << "[FAIL] " << cfg.folder_name << " 有效内点数过低: " << tmpl.detected_point_count << std::endl;
            return false;
        }

        if (tmpl.rms_error_pixels > 1.0) {
            std::cerr << "[FAIL] " << cfg.folder_name << " RMS 误差过大: " << tmpl.rms_error_pixels << " px" << std::endl;
            return false;
        }

        // 保存标定模板 (JSON 与 BIN)
        std::string json_path = folder_path + "/distortion_template.json";
        std::string bin_path = folder_path + "/distortion_template.bin";

        int save_json_res = Wafer_SaveDistortionTemplateToFile(json_path.c_str(), &tmpl);
        if (save_json_res != WAFER_SUCCESS) {
            std::cerr << "[FAIL] 保存 JSON 标定模板失败: " << json_path << std::endl;
            return false;
        }

        int save_bin_res = Wafer_SaveDistortionTemplateToFile(bin_path.c_str(), &tmpl);
        if (save_bin_res != WAFER_SUCCESS) {
            std::cerr << "[FAIL] 保存 BIN 标定模板失败: " << bin_path << std::endl;
            return false;
        }

        // 测试加载标定模板并核验一致性
        WaferDotGridDistortionTemplate loaded_tmpl{};
        int load_res = Wafer_LoadDistortionTemplateFromFile(json_path.c_str(), &loaded_tmpl);
        if (load_res != WAFER_SUCCESS || loaded_tmpl.detected_point_count != tmpl.detected_point_count) {
            std::cerr << "[FAIL] 加载校验 JSON 标定模板失败: " << json_path << std::endl;
            return false;
        }

        // 测试校正接口并保存校正后中心图
        cv::Mat test_img = wafer_calib::readImageUnicode(full_paths[0], cv::IMREAD_GRAYSCALE);
        if (!test_img.empty()) {
            std::vector<unsigned char> corrected_buf(test_img.cols * test_img.rows, 0);
            int corr_res = Wafer_CorrectImageByDotGridTemplate(
                test_img.data,
                test_img.cols,
                test_img.rows,
                &tmpl,
                corrected_buf.data()
            );
            if (corr_res != WAFER_SUCCESS) {
                std::cerr << "[FAIL] " << cfg.folder_name << " 校正执行失败，错误码: " << corr_res << std::endl;
                return false;
            }

            cv::Mat corrected_mat(test_img.rows, test_img.cols, CV_8UC1, corrected_buf.data());
            std::string corrected_path = folder_path + "/output_corrected_center.bmp";
            if (!wafer_calib::writeImageUnicode(corrected_path, corrected_mat)) {
                std::cerr << "[FAIL] 保存校正图像失败: " << corrected_path << std::endl;
                return false;
            }
        }

        std::cout << "  [产物已输出]" << std::endl
                  << "    - 诊断图: " << diag_path << std::endl
                  << "    - 标定文件(JSON): " << json_path << std::endl
                  << "    - 标定文件(BIN): " << bin_path << std::endl
                  << "    - 校正图(中心): " << folder_path << "/output_corrected_center.bmp" << std::endl;
    }

    std::cout << "========== 全部 5 个倍率真实图像标定测试通过 ==========" << std::endl;
    return true;
}

} // namespace

int main() {
    const std::string real_base_dir = "D:/images/谷神星/标准化/畸变矫正";
    if (std::filesystem::exists(wafer_calib::stringToWstring(real_base_dir))) {
        if (!runRealDatasetTest(real_base_dir)) {
            return 1;
        }
    } else {
        std::cout << "[WARN] 真实图像路径不存在，跳过真实图像测试" << std::endl;
    }

    return 0;
}
