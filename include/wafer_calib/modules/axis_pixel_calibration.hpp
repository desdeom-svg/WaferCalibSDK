#ifndef WAFER_CALIB_MODULES_AXIS_PIXEL_CALIBRATION_HPP_
#define WAFER_CALIB_MODULES_AXIS_PIXEL_CALIBRATION_HPP_

#include <string>
#include <vector>
#include <opencv2/core.hpp>

#include "wafer_calib/core/status.hpp"
#include "wafer_calib/core/types.hpp"

namespace wafer_calib {

/**
 * @brief 9点标定点对结构体
 */
struct AxisPixelPointPair {
    int index = 0;              // 点序号 (0~8)
    Point2D pixel;              // 图像像素坐标 (u, v)
    Point2D axis;               // 机械轴坐标 (X, Y)，单位：mm
    double residual_um = 0.0;   // 物理残差重投影误差，单位：微米 (um)
    double residual_px = 0.0;   // 像面残差重投影误差，单位：像素 (px)
    double dot_radius = 0.0;    // 提取的微标小圆亚像素半径，单位：像素
};

/**
 * @brief 9点网格扫描标定配置参数
 */
struct AxisPixelGridConfig {
    double initial_axis_x = 0.0;    // Mark_0 处的初始 X 轴物理坐标，单位：mm
    double initial_axis_y = 0.0;    // Mark_0 处的初始 Y 轴物理坐标，单位：mm
    double step_size_mm = 0.0;      // 机械步长，单位：mm (如 1.65)
    int axis_direction_x = 1;       // X 轴移动方向 (+1: 图像右偏对应轴增; -1: 图像左偏对应轴增)
    int axis_direction_y = 1;       // Y 轴移动方向 (+1: 图像下偏对应轴增; -1: 图像上偏对应轴增)
    std::string objective_id = "0"; // 物镜代号或倍率标识
};

/**
 * @brief 9点手眼/轴-像素标定计算结果与配方模型
 */
struct AxisPixelCalibResult {
    std::string version = "1.0.0";
    std::string timestamp;                  // 标定时间戳
    std::string objective_id = "0";         // 物镜标识
    double initial_axis_x = 0.0;            // 起始轴 X 坐标 (mm)
    double initial_axis_y = 0.0;            // 起始轴 Y 坐标 (mm)
    double step_size_mm = 0.0;              // 步长 (mm)

    // 核心物理光学与机械装配分解指标
    double pixel_scale_x_um = 0.0;          // X 方向像素当量，单位：um/px
    double pixel_scale_y_um = 0.0;          // Y 方向像素当量，单位：um/px
    double pixel_scale_mean_um = 0.0;       // 平均像素当量，单位：um/px
    double aspect_ratio = 1.0;              // 像素长宽比 (sx / sy)
    double rotation_angle_deg = 0.0;        // 相机与轴安装旋转偏角，单位：度 (deg)
    double orthogonality_skew_deg = 0.0;    // 轴正交性剪切残差偏角，单位：度 (deg)

    // 精度评定指标
    double rms_residual_um = 0.0;           // 全局重投影均方根误差，单位：微米 (um)
    double max_residual_um = 0.0;           // 最大单点重投影误差，单位：微米 (um)
    double rms_residual_px = 0.0;           // 全局重投影均方根误差，单位：像素 (px)
    double max_residual_px = 0.0;           // 最大单点重投影误差，单位：像素 (px)

    // 仿射变换矩阵 (2x3, CV_64F)
    cv::Mat m_pix2axis;                     // 像素 -> 轴坐标仿射矩阵 [X; Y] = M * [u; v; 1]
    cv::Mat m_axis2pix;                     // 轴坐标 -> 像素仿射逆矩阵 [u; v] = M_inv * [X; Y; 1]

    // 9 点详细坐标与残差数据
    std::vector<AxisPixelPointPair> points;
};

/**
 * @brief 9点标定（手眼/轴-像素标定）核心算法模块
 */
class AxisPixelCalibrationModule {
public:
    /**
     * @brief 基于 9 张网格图像与起始步长配置执行 9 点标定
     * @param mono8_images 输入 9 张连续内存 Mono8 灰度图 (按 Mark_0 ~ Mark_8 顺序)
     * @param config 标定配置参数 (起始轴位置、步长、方向与物镜代号)
     * @param result 输出标定结果与解算矩阵
     * @param diagnostic_bgr 输出高分辨率工业诊断大图 (传 empty 则不生成)
     * @param diag_w 诊断图宽度 (默认 3000)
     * @param diag_h 诊断图高度 (默认 2400)
     * @return Status 执行状态
     */
    static Status calibrateGrid(
        const std::vector<cv::Mat>& mono8_images,
        const AxisPixelGridConfig& config,
        AxisPixelCalibResult& result,
        cv::Mat& diagnostic_bgr,
        int diag_w = 3000,
        int diag_h = 2400
    );

    /**
     * @brief 基于 9 张图像与显式指定轴坐标执行 9 点标定
     * @param mono8_images 输入 9 张 Mono8 图像
     * @param axis_pts 9 个点对应的机械轴绝对坐标 (X, Y)
     * @param objective_id 物镜代号
     * @param result 输出标定结果
     * @param diagnostic_bgr 输出高分辨率工业诊断大图
     * @param diag_w 诊断图宽
     * @param diag_h 诊断图高
     * @return Status 执行状态
     */
    static Status calibratePoints(
        const std::vector<cv::Mat>& mono8_images,
        const std::vector<Point2D>& axis_pts,
        const std::string& objective_id,
        AxisPixelCalibResult& result,
        cv::Mat& diagnostic_bgr,
        int diag_w = 3000,
        int diag_h = 2400
    );

    /**
     * @brief 单张图像高精度提取微标小圆亚像素中心
     * @param mono8 输入 Mono8 图像
     * @param dot_center 输出小圆亚像素中心
     * @param dot_radius 输出小圆亚像素拟合半径
     * @return Status 执行状态
     */
    static Status detectMarkerDot(
        const cv::Mat& mono8,
        Point2D& dot_center,
        double& dot_radius
    );

    /**
     * @brief 将标定配方结果保存为标准 JSON 文件
     * @param filepath 目标保存文件路径
     * @param result 标定结果
     * @return Status 执行状态
     */
    static Status saveCalibrationJson(
        const std::string& filepath,
        const AxisPixelCalibResult& result
    );

    /**
     * @brief 从标定配方 JSON 文件中加载标定结果
     * @param filepath 配方文件路径
     * @param result 输出标定结果
     * @return Status 执行状态
     */
    static Status loadCalibrationJson(
        const std::string& filepath,
        AxisPixelCalibResult& result
    );

    /**
     * @brief 单点快速坐标正变换 (像素 -> 轴坐标)
     * @param calib 标定模型
     * @param pixel_pt 输入像面像素坐标 (u, v)
     * @return Point2D 输出物理轴坐标 (X, Y)，单位：mm
     */
    static Point2D transformPixelToAxis(
        const AxisPixelCalibResult& calib,
        const Point2D& pixel_pt
    );

    /**
     * @brief 单点快速坐标逆变换 (轴坐标 -> 像素)
     * @param calib 标定模型
     * @param axis_pt 输入物理轴坐标 (X, Y)，单位：mm
     * @return Point2D 输出像面像素坐标 (u, v)
     */
    static Point2D transformAxisToPixel(
        const AxisPixelCalibResult& calib,
        const Point2D& axis_pt
    );

    /**
     * @brief 渲染高分辨率综合工业诊断大图
     * @param mono8_images 9 张原始灰度图
     * @param result 标定计算结果
     * @param out_diag_bgr 输出 3 通道 BGR 诊断图
     * @param diag_w 画布宽度
     * @param diag_h 画布高度
     */
    static void renderDiagnostic(
        const std::vector<cv::Mat>& mono8_images,
        const AxisPixelCalibResult& result,
        cv::Mat& out_diag_bgr,
        int diag_w = 3000,
        int diag_h = 2400
    );
};

} // namespace wafer_calib

#endif // WAFER_CALIB_MODULES_AXIS_PIXEL_CALIBRATION_HPP_
