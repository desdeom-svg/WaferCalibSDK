#include "wafer_calib/modules/flat_field.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

namespace wafer_calib {

Status FlatFieldModule::calibrate(
    const std::vector<cv::Mat>& flat_frames,
    const cv::Mat& dark_template,
    float max_gain_clamp,
    cv::Mat& out_gain_map,
    FlatFieldMetrics* out_metrics,
    const std::string& save_template_path
) {
    if (flat_frames.empty()) {
        return Status::Error(ErrorCode::InvalidParam, "平场图像序列为空");
    }

    const cv::Size first_size = flat_frames[0].size();
    const int first_type = flat_frames[0].type();

    if (first_type != CV_8UC1) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "平场图像仅支持 CV_8UC1 格式");
    }

    for (const auto& frame : flat_frames) {
        if (frame.empty()) {
            return Status::Error(ErrorCode::ImageEmpty, "平场序列中包含空图像");
        }
        if (frame.size() != first_size || frame.type() != first_type) {
            return Status::Error(ErrorCode::ImageFormatMismatch, "平场序列图像尺寸或格式不一致");
        }
    }

    if (!dark_template.empty()) {
        if (dark_template.size() != first_size || dark_template.type() != CV_8UC1) {
            return Status::Error(ErrorCode::ImageFormatMismatch, "暗场模板尺寸或格式与平场帧不匹配");
        }
    }

    const int rows = first_size.height;
    const int cols = first_size.width;
    const int total_pixels = rows * cols;
    const int frame_count = static_cast<int>(flat_frames.size());

    // 1. 多帧平场均值融合 (消除时序散粒噪声)
    cv::Mat flat_mean = cv::Mat::zeros(first_size, CV_32FC1);
    for (const auto& frame : flat_frames) {
        cv::Mat float_frame;
        frame.convertTo(float_frame, CV_32F);
        flat_mean += float_frame;
    }
    flat_mean /= static_cast<float>(frame_count);

    // 2. 扣除暗场底噪偏置，得到净响应
    cv::Mat f_net(first_size, CV_32FC1);
    const float* flat_ptr = reinterpret_cast<const float*>(flat_mean.data);
    const uint8_t* dark_ptr = dark_template.empty() ? nullptr : dark_template.data;
    float* net_ptr = reinterpret_cast<float*>(f_net.data);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < total_pixels; ++i) {
        float d_val = dark_ptr ? static_cast<float>(dark_ptr[i]) : 0.0f;
        float val = flat_ptr[i] - d_val;
        net_ptr[i] = (val > 0.1f) ? val : 0.1f;
    }

    // 3. 取全图中心 1/4 区域 (width/2 * height/2) 的平均净灰度作为归一化基准
    const int roi_w = cols / 2;
    const int roi_h = rows / 2;
    const int roi_x = cols / 4;
    const int roi_y = rows / 4;
    cv::Rect center_roi(roi_x, roi_y, roi_w, roi_h);

    cv::Scalar center_mean_val = cv::mean(f_net(center_roi));
    const float target_benchmark = static_cast<float>(center_mean_val[0]);
    if (target_benchmark <= 0.1f) {
        return Status::Error(ErrorCode::InvalidParam, "平场净信号过弱或曝光严重不足");
    }

    // 4. 计算逐像元浮点增益矩阵 Gain = Benchmark / F_net
    out_gain_map.create(first_size, CV_32FC1);
    float* gain_ptr = reinterpret_cast<float*>(out_gain_map.data);
    const float clamp_val = (max_gain_clamp <= 0.0f) ? 3.5f : max_gain_clamp;

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < total_pixels; ++i) {
        float g = target_benchmark / net_ptr[i];
        if (g > clamp_val) {
            g = clamp_val;
        } else if (g < 0.0f) {
            g = 0.0f;
        }
        gain_ptr[i] = g;
    }

    // 5. 质量指标评估
    if (out_metrics) {
        // 校正前首帧非均匀度
        cv::Scalar mean_raw, std_raw;
        cv::meanStdDev(flat_frames[0], mean_raw, std_raw);
        out_metrics->raw_nu = (mean_raw[0] > 1e-5) ? (std_raw[0] / mean_raw[0] * 100.0) : 0.0;

        // 校正后非均匀度 (在平场平均净信号上应用增益)
        cv::Mat corr_test(first_size, CV_32FC1);
        float* corr_ptr = reinterpret_cast<float*>(corr_test.data);
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < total_pixels; ++i) {
            corr_ptr[i] = net_ptr[i] * gain_ptr[i];
        }
        cv::Scalar mean_corr, std_corr;
        cv::meanStdDev(corr_test, mean_corr, std_corr);
        out_metrics->corrected_nu = (mean_corr[0] > 1e-5) ? (std_corr[0] / mean_corr[0] * 100.0) : 0.0;

        // 镜头角隅光学渐晕衰减率
        const int cw = std::max(16, cols / 8);
        const int ch = std::max(16, rows / 8);
        cv::Rect tl(0, 0, cw, ch);
        cv::Rect tr(cols - cw, 0, cw, ch);
        cv::Rect bl(0, rows - ch, cw, ch);
        cv::Rect br(cols - cw, rows - ch, cw, ch);
        double c_tl = cv::mean(flat_mean(tl))[0];
        double c_tr = cv::mean(flat_mean(tr))[0];
        double c_bl = cv::mean(flat_mean(bl))[0];
        double c_br = cv::mean(flat_mean(br))[0];
        double corner_mean = (c_tl + c_tr + c_bl + c_br) * 0.25;
        double center_mean = cv::mean(flat_mean(center_roi))[0];
        out_metrics->vignetting = (center_mean > 1e-5) ? std::max(0.0, (1.0 - corner_mean / center_mean) * 100.0) : 0.0;
    }

    // 6. 保存模板文件 (可选)
    if (!save_template_path.empty()) {
        std::string ext = "";
        size_t dot_pos = save_template_path.find_last_of('.');
        if (dot_pos != std::string::npos) {
            ext = save_template_path.substr(dot_pos);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        }

        if (ext == ".tiff" || ext == ".tif") {
            // 保存为单通道 32 位浮点 TIFF 图像
            if (!writeImageUnicode(save_template_path, out_gain_map)) {
                return Status::Error(ErrorCode::FileIOError, "保存平场 TIFF 增益图像失败");
            }
        } else {
            // 保存为二进制平场文件 (.bin)
#ifdef _WIN32
            std::wstring wpath = stringToWstring(save_template_path);
            std::ofstream file(wpath, std::ios::binary);
#else
            std::ofstream file(save_template_path, std::ios::binary);
#endif
            if (!file.is_open()) {
                return Status::Error(ErrorCode::FileIOError, "无法创建平场模板二进制文件");
            }
            // 写入简单头部：magic, width, height
            const uint32_t magic = 0x43464657; // "WFFC"
            const int32_t w = cols;
            const int32_t h = rows;
            file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
            file.write(reinterpret_cast<const char*>(&w), sizeof(w));
            file.write(reinterpret_cast<const char*>(&h), sizeof(h));
            file.write(reinterpret_cast<const char*>(gain_ptr), total_pixels * sizeof(float));
        }
    }

    return Status::OK();
}

Status FlatFieldModule::apply(
    const cv::Mat& input_image,
    cv::Mat& output_image,
    const cv::Mat& dark_template,
    const cv::Mat& gain_map
) {
    return applyPipeline(input_image, output_image, dark_template, gain_map, nullptr);
}

Status FlatFieldModule::applyPipeline(
    const cv::Mat& input_image,
    cv::Mat& output_image,
    const cv::Mat& dark_template,
    const cv::Mat& gain_map,
    const uint8_t* lut
) {
    if (input_image.empty()) {
        return Status::Error(ErrorCode::ImageEmpty, "输入待校正图像为空");
    }
    if (input_image.type() != CV_8UC1) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "仅支持 CV_8UC1 图像");
    }
    if (gain_map.empty() || gain_map.size() != input_image.size() || gain_map.type() != CV_32FC1) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "增益矩阵尺寸或格式不匹配 (需为 CV_32FC1)");
    }
    if (!dark_template.empty()) {
        if (dark_template.size() != input_image.size() || dark_template.type() != CV_8UC1) {
            return Status::Error(ErrorCode::ImageFormatMismatch, "暗场模板尺寸或格式与输入图像不一致");
        }
    }

    if (output_image.empty() || output_image.size() != input_image.size() || output_image.type() != CV_8UC1) {
        output_image.create(input_image.size(), CV_8UC1);
    }

    const int total = input_image.rows * input_image.cols;
    const uint8_t* in_ptr = input_image.data;
    uint8_t* out_ptr = output_image.data;
    const float* gain_ptr = reinterpret_cast<const float*>(gain_map.data);
    const uint8_t* dark_ptr = dark_template.empty() ? nullptr : dark_template.data;

    if (lut != nullptr) {
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < total; ++i) {
            float in_val = static_cast<float>(in_ptr[i]);
            float dark_val = dark_ptr ? static_cast<float>(dark_ptr[i]) : 0.0f;
            float val = (in_val - dark_val) * gain_ptr[i];
            if (val < 0.0f) {
                val = 0.0f;
            } else if (val > 255.0f) {
                val = 255.0f;
            }
            uint8_t flat_val = static_cast<uint8_t>(val + 0.5f);
            out_ptr[i] = lut[flat_val];
        }
    } else {
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < total; ++i) {
            float in_val = static_cast<float>(in_ptr[i]);
            float dark_val = dark_ptr ? static_cast<float>(dark_ptr[i]) : 0.0f;
            float val = (in_val - dark_val) * gain_ptr[i];
            if (val < 0.0f) {
                val = 0.0f;
            } else if (val > 255.0f) {
                val = 255.0f;
            }
            out_ptr[i] = static_cast<uint8_t>(val + 0.5f);
        }
    }

    return Status::OK();
}

} // namespace wafer_calib
