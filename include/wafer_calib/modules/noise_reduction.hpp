#ifndef WAFER_CALIB_MODULES_NOISE_REDUCTION_HPP_
#define WAFER_CALIB_MODULES_NOISE_REDUCTION_HPP_

#include "wafer_calib/core/status.hpp"
#include "wafer_calib/core/types.hpp"

namespace wafer_calib {

enum class FrameCombineMethod {
    Mean = 0,      // 均值融合
    Median = 1,    // 中值融合 (暗场模板推荐)
    SigmaClip = 2  // 3-Sigma 离群剔除均值
};

class NoiseReductionModule {
public:
    NoiseReductionModule() = default;
    ~NoiseReductionModule() = default;

    // 从任意 N 帧连续暗场图像合成标准暗场背景噪声模板图
    static Status createDarkFrameTemplate(
        const std::vector<cv::Mat>& dark_frames,
        cv::Mat& output_dark_template,
        FrameCombineMethod method = FrameCombineMethod::Median
    );
};

} // namespace wafer_calib

#endif // WAFER_CALIB_MODULES_NOISE_REDUCTION_HPP_
