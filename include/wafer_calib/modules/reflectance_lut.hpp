#pragma once

#include <string>
#include <vector>
#include <opencv2/core.hpp>

namespace wafer_calib {

/**
 * @brief LUT 曲线生成算法模式
 */
enum WaferReflectanceLutMode {
    /** @brief 分段阶梯死区平台模式 (锁定目标灰度并消除微小光学扰动) */
    WAFER_LUT_MODE_DEADBAND = 0,
    /** @brief 单调平滑样条曲线模式 (PCHIP 保形插值，避免阶梯等高线效应) */
    WAFER_LUT_MODE_SMOOTH = 1
};

/**
 * @brief 反射率 LUT 标定参数配置
 */
struct ReflectanceLutConfig {
    /** @brief LUT 映射算法模式 (0: 死区平台模式, 1: 平滑样条模式) */
    WaferReflectanceLutMode mode = WAFER_LUT_MODE_DEADBAND;

    /** @brief 统计直方图的中心 ROI 宽度 (像素，传 0 表示使用全幅图像) */
    int roi_center_width = 1000;

    /** @brief 统计直方图的中心 ROI 高度 (像素，传 0 表示使用全幅图像) */
    int roi_center_height = 1000;

    /** @brief 目标基准值附近的死区半宽 (像素灰度级，默认为 5) */
    int deadband_width = 5;

    /** @brief 4 种反射率 (5%, 50%, 75%, 90%) 对应的理想目标基准灰度 */
    double target_values[4] = {13.0, 128.0, 192.0, 230.0};
};

/**
 * @brief 反射率 LUT 标定结果
 */
struct ReflectanceLutResult {
    /** @brief 生成的 256 元素灰度查找表 (输出灰度 = lut[输入灰度]) */
    unsigned char lut[256];

    /** @brief 4 张标定图像实际统计提取到的波峰灰度值 */
    double measured_peaks[4];

    /** @brief 标定使用的 4 阶目标基准灰度值 */
    double target_values[4];

    /** @brief 校正前实测灰度与物理反射率的线性拟合优度 R^2 */
    double raw_linearity_r2;

    /** @brief 校正后理想灰度与物理反射率的线性拟合优度 R^2 */
    double corrected_linearity_r2;
};

/**
 * @brief 反射率光电响应标定与灰度线性化 LUT 核心算法类
 */
class WaferReflectanceLutCalibrator {
public:
    /**
     * @brief 从单张标定图像中提取中心区域高斯平滑直方图的波峰位置
     * @param image 输入 Mono8 图像
     * @param roi_w ROI 宽度，0 表示全图
     * @param roi_h ROI 高度，0 表示全图
     * @param out_peak 输出提取到的波峰灰度 (亚像素或整像素)
     * @param out_hist 可选输出归一化直方图 (长度 256)
     * @return true 提取成功，false 失败
     */
    static bool ExtractPeakFromImage(
        const cv::Mat& image,
        int roi_w,
        int roi_h,
        double& out_peak,
        std::vector<double>* out_hist = nullptr
    );

    /**
     * @brief 结合 4 张已知反射率标定图计算 256 元素灰度映射 LUT，并可选渲染工业诊断大图
     * @param images 包含 4 张标定图像的向量 (顺序必须严格对应 5%, 50%, 75%, 90%)
     * @param config 标定配置参数
     * @param out_result 输出标定计算结果
     * @param out_diagnostic 可选输出的高分辨率工业级四合一诊断大图
     * @return true 标定成功，false 失败
     */
    static bool Calibrate(
        const std::vector<cv::Mat>& images,
        const ReflectanceLutConfig& config,
        ReflectanceLutResult& out_result,
        cv::Mat* out_diagnostic = nullptr
    );

    /**
     * @brief 使用 256 元素 LUT 对单张 Mono8 图像进行高速查表映射
     * @param src 输入原始 Mono8 图像
     * @param dst 输出校正后的 Mono8 图像 (由调用方预分配或函数内自动创建)
     * @param lut_256 包含 256 个字节的查找表指针
     * @return true 映射成功，false 失败
     */
    static bool ApplyLut(
        const cv::Mat& src,
        cv::Mat& dst,
        const unsigned char* lut_256
    );

    /**
     * @brief 将 256 元素 LUT 保存到磁盘文件 (支持 .lut 纯文本、.csv 或 .bin 二进制)
     */
    static bool SaveLut(const std::string& path, const unsigned char* lut_256);

    /**
     * @brief 从磁盘文件加载 256 元素 LUT
     */
    static bool LoadLut(const std::string& path, unsigned char* lut_256);

    /**
     * @brief 渲染高分辨率四合一工业级诊断大看板
     */
    static void RenderDiagnosticDashboard(
        const std::vector<std::vector<double>>& hists,
        const double measured_peaks[4],
        const double target_values[4],
        const unsigned char lut[256],
        const ReflectanceLutConfig& config,
        double raw_r2,
        double corr_r2,
        cv::Mat& out_dashboard
    );
};

} // namespace wafer_calib
