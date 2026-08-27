#ifndef WAFER_CALIB_MODULES_CIRCLE_CENTER_OFFSET_HPP_
#define WAFER_CALIB_MODULES_CIRCLE_CENTER_OFFSET_HPP_

#include "wafer_calib/core/status.hpp"
#include "wafer_calib/core/types.hpp"

namespace wafer_calib {

/**
 * @brief 圆检测与中心偏移计算结果结构体
 */
struct CircleDetectionResult {
    Point2D circle_center;      // 圆中心亚像素坐标 (x, y)
    double radius = 0.0;        // 拟合圆半径，单位：像素
    Point2D image_center;       // 图像中心坐标 (x, y)
    double offset_x = 0.0;      // X 方向差值 (circle_center.x - image_center.x)，单位：像素
    double offset_y = 0.0;      // Y 方向差值 (circle_center.y - image_center.y)，单位：像素
    double circularity = 0.0;   // 候选轮廓圆度 (4*pi*area / perimeter^2)
    double rms_residual = 0.0;  // 轮廓点到拟合圆的 RMS 几何残差，单位：像素
};

/**
 * @brief 圆中心与图像中心差值计算模块
 */
class CircleCenterOffsetModule {
public:
    /**
     * @brief 在 Mono8 图像中检测圆形目标，计算圆中心与图像中心的差值，并输出诊断标注图
     * @param mono8 输入 Mono8 灰度图像
     * @param result 输出圆检测及中心差值计算结果
     * @param result_bgr 输出 BGR 标注图（由模块内部创建或写入）
     * @return Status 执行状态
     */
    static Status findCircleCenterOffset(
        const cv::Mat& mono8,
        CircleDetectionResult& result,
        cv::Mat& result_bgr);
};

} // namespace wafer_calib

#endif // WAFER_CALIB_MODULES_CIRCLE_CENTER_OFFSET_HPP_
