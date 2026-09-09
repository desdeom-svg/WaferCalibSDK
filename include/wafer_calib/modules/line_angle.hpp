#ifndef WAFER_CALIB_MODULES_LINE_ANGLE_HPP_
#define WAFER_CALIB_MODULES_LINE_ANGLE_HPP_

#include "wafer_calib/core/status.hpp"
#include "wafer_calib/core/types.hpp"

namespace wafer_calib {

/**
 * @brief 单视野水平标定线亚像素特征信息
 */
struct SingleViewLineInfo {
    double angle_degrees = 0.0;     // 局部直线拟合角度 (度，范围 [-90, 90))
    double center_y = 0.0;          // 在图像中心列 u = width / 2.0 处的直线亚像素 Y 坐标
    cv::Point2d line_center;        // 拟合基准点 (x0, y0)
    cv::Point2d line_direction;     // 拟合单位方向向量 (vx, vy)
    cv::Rect bounding_box;          // 直线区域外接矩形
    bool detected = false;          // 是否成功检测
};

/**
 * @brief 双视野大基线标定线全局角度解算结果
 */
struct TwoViewLineAngleResult {
    double global_angle_deg = 0.0;      // 大基线超高精度全局角度 (度)
    double global_angle_arcmin = 0.0;   // 大基线超高精度全局角度 (角分)
    double view1_angle_deg = 0.0;       // 视野 1 (左端) 局部拟合角度 (度)
    double view2_angle_deg = 0.0;       // 视野 2 (右端) 局部拟合角度 (度)
    double view1_center_y = 0.0;        // 视野 1 图像中心列处的直线高度 (像素)
    double view2_center_y = 0.0;        // 视野 2 图像中心列处的直线高度 (像素)
    double delta_v_pixels = 0.0;        // 像面垂直落差 (v2 - v1，像素)
    double delta_y_world_mm = 0.0;      // 物理垂直落差 (mm)
    double stage_delta_x_mm = 0.0;      // 机械 X 轴移动物理距离 (mm)
    double pixel_scale_y_um = 0.0;      // Y 向像元当量 (微米/像素)
};

class LineAngleModule {
public:
    /**
     * @brief 提取单张图像中的水平标定线亚像素信息与局部角度
     */
    static Status extractLineInfo(
        const cv::Mat& mono8,
        SingleViewLineInfo& out_info,
        cv::Mat* result_bgr = nullptr);

    /**
     * @brief 原有接口：检测单张 Mono8 图像中黑色水平标定线的角度
     */
    static Status findHorizontalLineAngle(
        const cv::Mat& mono8,
        double& angle_degrees,
        cv::Mat& result_bgr);

    /**
     * @brief 跨双视野大基线高精度水平标定线全局角度检测与综合看板图合成
     */
    static Status findTwoViewHorizontalLineAngle(
        const cv::Mat& mono8_view1,
        const cv::Mat& mono8_view2,
        double stage_delta_x_mm,
        double pixel_scale_y_um,
        TwoViewLineAngleResult& out_result,
        cv::Mat& diagnostic_bgr,
        int diag_width = 3000,
        int diag_height = 1800);
};

} // namespace wafer_calib

#endif // WAFER_CALIB_MODULES_LINE_ANGLE_HPP_
