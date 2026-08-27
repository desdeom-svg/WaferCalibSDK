#include "wafer_calib/modules/noise_reduction.hpp"
#include <algorithm>
#include <cmath>

namespace wafer_calib {

Status NoiseReductionModule::createDarkFrameTemplate(
    const std::vector<cv::Mat>& dark_frames,
    cv::Mat& output_dark_template,
    FrameCombineMethod method
) {
    if (dark_frames.empty()) {
        return Status::Error(ErrorCode::InvalidParam, "暗场图像序列为空");
    }

    const cv::Size first_size = dark_frames[0].size();
    const int first_type = dark_frames[0].type();

    for (const auto& frame : dark_frames) {
        if (frame.empty()) {
            return Status::Error(ErrorCode::ImageEmpty, "序列中包含空暗场帧");
        }
        if (frame.size() != first_size || frame.type() != first_type) {
            return Status::Error(ErrorCode::ImageFormatMismatch, "暗场帧尺寸或类型不一致");
        }
    }

    if (first_type != CV_8UC1) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "仅支持 CV_8UC1 暗场图像");
    }

    const int num_frames = static_cast<int>(dark_frames.size());

    if (method == FrameCombineMethod::Mean) {
        // 均值融合
        cv::Mat accum = cv::Mat::zeros(first_size, CV_64FC(dark_frames[0].channels()));
        for (const auto& frame : dark_frames) {
            cv::Mat float_frame;
            frame.convertTo(float_frame, CV_64F);
            accum += float_frame;
        }
        accum /= static_cast<double>(num_frames);
        accum.convertTo(output_dark_template, first_type);
    } else if (method == FrameCombineMethod::Median) {
        // 中值融合
        output_dark_template.create(first_size, first_type);
        const int rows = first_size.height;
        const int cols = first_size.width;

        #pragma omp parallel for collapse(2) if(rows >= 512)
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                std::vector<uchar> pixel_vals(num_frames);
                for (int f = 0; f < num_frames; ++f) {
                    pixel_vals[f] = dark_frames[f].at<uchar>(r, c);
                }
                std::sort(pixel_vals.begin(), pixel_vals.end());
                output_dark_template.at<uchar>(r, c) = pixel_vals[num_frames / 2];
            }
        }
    } else if (method == FrameCombineMethod::SigmaClip) {
        // 3-Sigma 离群剔除均值
        output_dark_template.create(first_size, first_type);
        const int rows = first_size.height;
        const int cols = first_size.width;

        #pragma omp parallel for collapse(2) if(rows >= 512)
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                double sum = 0.0;
                double sq_sum = 0.0;
                std::vector<double> vals(num_frames);
                for (int f = 0; f < num_frames; ++f) {
                    double val = static_cast<double>(dark_frames[f].at<uchar>(r, c));
                    vals[f] = val;
                    sum += val;
                    sq_sum += val * val;
                }
                double mean = sum / num_frames;
                double variance = (sq_sum / num_frames) - (mean * mean);
                double stddev = std::sqrt(std::max(0.0, variance));

                double valid_sum = 0.0;
                int valid_cnt = 0;
                for (int f = 0; f < num_frames; ++f) {
                    if (std::abs(vals[f] - mean) <= 3.0 * stddev || stddev < 1e-5) {
                        valid_sum += vals[f];
                        valid_cnt++;
                    }
                }
                double final_val = (valid_cnt > 0) ? (valid_sum / valid_cnt) : mean;
                output_dark_template.at<uchar>(r, c) = cv::saturate_cast<uchar>(std::round(final_val));
            }
        }
    }

    return Status::OK();
}

} // namespace wafer_calib
