#ifndef WAFER_CALIB_MODULES_ROTATION_CENTER_HPP_
#define WAFER_CALIB_MODULES_ROTATION_CENTER_HPP_

#include <vector>
#include <opencv2/core.hpp>

#include "wafer_calib/core/status.hpp"
#include "wafer_calib/core/types.hpp"

namespace wafer_calib {

/**
 * @brief 平台旋转中心计算结果结构体
 */
struct PlatformRotationCenterResult {
    Point2D rotation_center;                  // 拟合平台旋转中心坐标 (Cx, Cy)，单位：像素
    double rotation_radius = 0.0;             // 拟合旋转偏心半径 R，单位：像素
    double rms_error = 0.0;                   // 全局 RMS 几何拟合残差，单位：像素
    double max_error = 0.0;                   // 最大单点径向偏差，单位：像素
    int valid_point_count = 0;                // 成功识别的特征圆点数量
    std::vector<Point2D> detected_points;     // 各图像中识别到的小圆亚像素坐标
    std::vector<double> point_radii;          // 各小圆自身的亚像素半径
    std::vector<double> point_angles_deg;     // 各点相对于旋转中心的极角 (0~360度)
    std::vector<double> radial_residuals;     // 各点相对于拟合大圆的径向残差 (dist - R)
};

/**
 * @brief 载晶圆平台旋转中心高精度拟合模块
 */
class PlatformRotationCenterModule {
public:
    /**
     * @brief 输入多张旋转图像，提取各图微标小圆亚像素中心并拟合大圆求解旋转中心
     * @param mono8_images 输入一组 Mono8 灰度图像（按旋转顺序排列，如 12 张）
     * @param result 输出旋转中心、偏心半径、残差及各点位坐标
     * @param diagnostic_bgr 输出高分辨率全画幅可视化诊断大图（绘制大圆轨迹、各圆心、残差矢量及数据面板）
     * @return Status 执行状态
     */
    static Status calculateRotationCenter(
        const std::vector<cv::Mat>& mono8_images,
        PlatformRotationCenterResult& result,
        cv::Mat& diagnostic_bgr
    );

    /**
     * @brief 从单张 Mono8 图像中高精度提取微标小圆亚像素中心
     * @param mono8 输入 Mono8 灰度图像
     * @param dot_center 输出小圆亚像素中心坐标
     * @param dot_radius 输出小圆亚像素拟合半径
     * @return Status 执行状态
     */
    static Status detectMarkerDotCenter(
        const cv::Mat& mono8,
        Point2D& dot_center,
        double& dot_radius
    );
};

} // namespace wafer_calib

#endif // WAFER_CALIB_MODULES_ROTATION_CENTER_HPP_