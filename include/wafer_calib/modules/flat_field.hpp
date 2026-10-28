#ifndef WAFER_CALIB_MODULES_FLAT_FIELD_HPP_
#define WAFER_CALIB_MODULES_FLAT_FIELD_HPP_

#include "wafer_calib/core/status.hpp"
#include "wafer_calib/core/types.hpp"
#include <string>
#include <vector>
#include <opencv2/core.hpp>

namespace wafer_calib {

/**
 * @brief 平场标定质量评估指标
 */
struct FlatFieldMetrics {
    /** @brief 校正前全图非均匀度 Non-Uniformity (%) */
    double raw_nu = 0.0;
    /** @brief 校正后全图非均匀度预测值 (%) */
    double corrected_nu = 0.0;
    /** @brief 镜头角隅光学渐晕衰减率 (%) */
    double vignetting = 0.0;
};

/**
 * @brief 平场校正与增益矩阵计算核心算法类
 */
class FlatFieldModule {
public:
    FlatFieldModule() = default;
    ~FlatFieldModule() = default;

    /**
     * @brief 基于多帧平场图像和暗场模板生成逐像素 32 位浮点增益矩阵
     * @param flat_frames 输入 N 帧平场 Mono8 图像序列 (必须同尺寸同格式)
     * @param dark_template 输入暗场底噪模板 (CV_8UC1，可为空)
     * @param max_gain_clamp 增益截断上限 (传 <=0.0 时默认取 3.5)
     * @param out_gain_map 输出逐像素 32 位浮点增益矩阵 (CV_32FC1)
     * @param out_metrics 可选输出质量评估指标；传 nullptr 表示不获取
     * @param save_template_path 可选保存路径 (如 .tiff 或 .bin)；为空时不保存
     * @return Status 状态对象
     */
    static Status calibrate(
        const std::vector<cv::Mat>& flat_frames,
        const cv::Mat& dark_template,
        float max_gain_clamp,
        cv::Mat& out_gain_map,
        FlatFieldMetrics* out_metrics = nullptr,
        const std::string& save_template_path = ""
    );

    /**
     * @brief 在线对单帧 Mono8 图像执行暗场扣除与平场增益映射
     * @param input_image 输入待校正图像 (CV_8UC1)
     * @param output_image 输出校正后图像 (CV_8UC1，可与 input_image 指向相同以原地校正)
     * @param dark_template 暗场底噪模板 (CV_8UC1，可为空)
     * @param gain_map 平场增益矩阵 (CV_32FC1)
     * @return Status 状态对象
     */
    static Status apply(
        const cv::Mat& input_image,
        cv::Mat& output_image,
        const cv::Mat& dark_template,
        const cv::Mat& gain_map
    );

    /**
     * @brief 在线对单帧 Mono8 图像执行“暗场扣除 - 平场增益 - LUT查表”一体化极速流水线预处理 (单次内存遍历)
     * @param input_image 输入待校正图像 (CV_8UC1)
     * @param output_image 输出校正后图像 (CV_8UC1，可与 input_image 指向相同以原地校正)
     * @param dark_template 暗场底噪模板 (CV_8UC1，可为空)
     * @param gain_map 平场增益矩阵 (CV_32FC1)
     * @param lut 256 元素灰度映射表 (长度为 256 的 uint8_t 数组指针，传 nullptr 则跳过查表直接输出平场值)
     * @return Status 状态对象
     */
    static Status applyPipeline(
        const cv::Mat& input_image,
        cv::Mat& output_image,
        const cv::Mat& dark_template,
        const cv::Mat& gain_map,
        const uint8_t* lut = nullptr
    );
};

} // namespace wafer_calib

#endif // WAFER_CALIB_MODULES_FLAT_FIELD_HPP_
