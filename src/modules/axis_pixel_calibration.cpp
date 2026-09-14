#include "wafer_calib/modules/axis_pixel_calibration.hpp"

#include <cmath>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace wafer_calib {

namespace {

// Taubin 代数圆拟合封闭解 (与 rotation_center 保持一致的高精度拟合算法)
bool fitCircleTaubin(
    const std::vector<cv::Point2d>& pts,
    double& out_cx,
    double& out_cy,
    double& out_radius
) {
    if (pts.size() < 3) {
        return false;
    }

    double sum_x = 0.0;
    double sum_y = 0.0;
    for (const auto& p : pts) {
        sum_x += p.x;
        sum_y += p.y;
    }
    const double mean_x = sum_x / pts.size();
    const double mean_y = sum_y / pts.size();

    double Mxx = 0.0;
    double Myy = 0.0;
    double Mxy = 0.0;
    double Mxz = 0.0;
    double Myz = 0.0;
    double Mzz = 0.0;

    for (const auto& p : pts) {
        const double xi = p.x - mean_x;
        const double yi = p.y - mean_y;
        const double zi = xi * xi + yi * yi;

        Mxx += xi * xi;
        Myy += yi * yi;
        Mxy += xi * yi;
        Mxz += xi * zi;
        Myz += yi * zi;
        Mzz += zi * zi;
    }

    const double inv_n = 1.0 / pts.size();
    Mxx *= inv_n;
    Myy *= inv_n;
    Mxy *= inv_n;
    Mxz *= inv_n;
    Myz *= inv_n;
    Mzz *= inv_n;

    const double Mz = Mxx + Myy;
    const double Cov_xy = Mxx * Myy - Mxy * Mxy;
    const double A3 = 4.0 * Mz;
    const double A2 = -3.0 * Mz * Mz - Mzz;
    const double A1 = Mzz * Mz + 4.0 * Cov_xy * Mz - Mxz * Mxz - Myz * Myz - Mz * Mz * Mz;
    const double A0 = Mxz * Mxz * Myy + Myz * Myz * Mxx - 2.0 * Mxz * Myz * Mxy - Cov_xy * Mzz;
    const double A22 = A2 + A2;
    const double A33 = A3 + A3 + A3;

    double x_root = 0.0;
    for (int iter = 0; iter < 25; ++iter) {
        const double y_val = A0 + x_root * (A1 + x_root * (A2 + x_root * A3));
        const double dy_val = A1 + x_root * (A22 + x_root * A33);
        if (std::abs(dy_val) < 1e-12) {
            break;
        }
        const double x_new = x_root - y_val / dy_val;
        if (std::abs(x_new - x_root) < 1e-12) {
            x_root = x_new;
            break;
        }
        x_root = x_new;
    }

    const double DET = x_root * x_root - x_root * Mz + Cov_xy;
    if (std::abs(DET) < 1e-12) {
        return false;
    }

    const double x_fit = (Mxz * (Myy - x_root) - Myz * Mxy) / DET / 2.0;
    const double y_fit = (Myz * (Mxx - x_root) - Mxz * Mxy) / DET / 2.0;

    out_cx = x_fit + mean_x;
    out_cy = y_fit + mean_y;
    out_radius = std::sqrt(x_fit * x_fit + y_fit * y_fit + Mz);
    return std::isfinite(out_cx) && std::isfinite(out_cy) && std::isfinite(out_radius) && out_radius > 0.0;
}

// 获取当前时间字符串
std::string getCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

} // namespace

Status AxisPixelCalibrationModule::detectMarkerDot(
    const cv::Mat& mono8,
    Point2D& dot_center,
    double& dot_radius
) {
    if (mono8.empty() || mono8.type() != CV_8UC1) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "输入图像为空或非 Mono8 格式");
    }

    // 1. 多尺度高斯差分 (DoG) 去除全局不均匀背景并自适应提取微标小圆
    cv::Mat g_small;
    cv::Mat g_large;
    cv::GaussianBlur(mono8, g_small, cv::Size(9, 9), 2.0);
    cv::GaussianBlur(mono8, g_large, cv::Size(65, 65), 15.0);

    cv::Mat diff_pos;
    cv::Mat diff_neg;
    cv::subtract(g_small, g_large, diff_pos);
    cv::subtract(g_large, g_small, diff_neg);

    const double total_pixels = static_cast<double>(mono8.cols * mono8.rows);
    const cv::Mat morph_kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));

    cv::Point2d rough_center;
    bool found_cand = false;
    double best_score = -1e9;

    const cv::Mat passes[2] = {diff_pos, diff_neg};
    for (const auto& diff_img : passes) {
        double min_val = 0.0;
        double max_val = 0.0;
        cv::minMaxLoc(diff_img, &min_val, &max_val);
        if (max_val < 3.0) {
            continue;
        }

        const int hist_size = 256;
        float range[] = {0, 256};
        const float* hist_range = {range};
        cv::Mat hist;
        cv::calcHist(&diff_img, 1, 0, cv::Mat(), hist, 1, &hist_size, &hist_range, true, false);

        const double target_98 = total_pixels * 0.98;
        const double target_995 = total_pixels * 0.995;
        double accum = 0.0;
        int th_98 = 0;
        int th_995 = 0;
        for (int i = 0; i < 256; ++i) {
            accum += hist.at<float>(i);
            if (accum >= target_98 && th_98 == 0) {
                th_98 = i;
            }
            if (accum >= target_995 && th_995 == 0) {
                th_995 = i;
                break;
            }
        }

        int th_val = std::max(3, (th_98 + th_995) / 2);
        if (th_val >= static_cast<int>(max_val)) {
            th_val = std::max(3, static_cast<int>(max_val * 0.6));
        }

        cv::Mat binary;
        cv::threshold(diff_img, binary, th_val, 255, cv::THRESH_BINARY);
        cv::Mat binary_clean;
        cv::morphologyEx(binary, binary_clean, cv::MORPH_CLOSE, morph_kernel);
        cv::morphologyEx(binary_clean, binary_clean, cv::MORPH_OPEN, morph_kernel);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary_clean, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        for (const auto& c : contours) {
            const double area = cv::contourArea(c);
            if (area < 500.0 || area > 10000.0) {
                continue;
            }

            const double perimeter = cv::arcLength(c, true);
            if (perimeter <= 0.0) {
                continue;
            }
            const double circ = 4.0 * CV_PI * area / (perimeter * perimeter);
            if (circ < 0.50) {
                continue;
            }

            const cv::Moments m = cv::moments(c);
            if (m.m00 <= 0.0) {
                continue;
            }

            cv::Point2f encl_center;
            float encl_r = 0.0f;
            cv::minEnclosingCircle(c, encl_center, encl_r);

            // 评分：综合圆度与目标微标小圆半径先验 (R ~ 25.5~27.0 px)
            const double score = circ * 10.0 - std::abs(static_cast<double>(encl_r) - 26.5) * 0.5;
            if (score > best_score) {
                best_score = score;
                rough_center = cv::Point2d(m.m10 / m.m00, m.m01 / m.m00);
                found_cand = true;
            }
        }
    }

    if (!found_cand) {
        return Status::Error(ErrorCode::FeatureNotFound, "未能在图像中提取到微标中心小圆");
    }

    // 2. 亚像素径向梯度边缘采样
    const int roi_size = 70;
    const int rx0 = std::clamp(static_cast<int>(std::round(rough_center.x - roi_size / 2)), 0, mono8.cols - roi_size);
    const int ry0 = std::clamp(static_cast<int>(std::round(rough_center.y - roi_size / 2)), 0, mono8.rows - roi_size);
    const cv::Rect roi_rect(rx0, ry0, roi_size, roi_size);
    const cv::Mat roi = mono8(roi_rect);

    cv::Mat gx;
    cv::Mat gy;
    cv::Sobel(roi, gx, CV_32F, 1, 0, 3);
    cv::Sobel(roi, gy, CV_32F, 0, 1, 3);

    cv::Mat gmag;
    cv::magnitude(gx, gy, gmag);

    const double local_cx = rough_center.x - rx0;
    const double local_cy = rough_center.y - ry0;

    std::vector<cv::Point2d> sub_edge_points;
    sub_edge_points.reserve(72);

    for (int deg = 0; deg < 360; deg += 5) {
        const double rad = deg * (CV_PI / 180.0);
        const double cos_a = std::cos(rad);
        const double sin_a = std::sin(rad);

        std::vector<float> sample_vals;
        std::vector<float> sample_radii;
        sample_vals.reserve(81);
        sample_radii.reserve(81);

        for (double r = 15.0; r <= 35.0; r += 0.25) {
            const float sx = static_cast<float>(local_cx + r * cos_a);
            const float sy = static_cast<float>(local_cy + r * sin_a);
            if (sx < 1.0f || sx >= roi_size - 2 || sy < 1.0f || sy >= roi_size - 2) {
                continue;
            }

            const int x1 = static_cast<int>(sx);
            const int y1 = static_cast<int>(sy);
            const float fx = sx - x1;
            const float fy = sy - y1;

            const float v = (1.0f - fx) * (1.0f - fy) * gmag.at<float>(y1, x1) +
                            fx * (1.0f - fy) * gmag.at<float>(y1, x1 + 1) +
                            (1.0f - fx) * fy * gmag.at<float>(y1 + 1, x1) +
                            fx * fy * gmag.at<float>(y1 + 1, x1 + 1);

            sample_vals.push_back(v);
            sample_radii.push_back(static_cast<float>(r));
        }

        if (sample_vals.size() < 10) {
            continue;
        }

        auto max_it = std::max_element(sample_vals.begin(), sample_vals.end());
        const int max_idx = static_cast<int>(std::distance(sample_vals.begin(), max_it));

        if (max_idx > 0 && max_idx < static_cast<int>(sample_vals.size()) - 1) {
            const float y1 = sample_vals[max_idx - 1];
            const float y2 = sample_vals[max_idx];
            const float y3 = sample_vals[max_idx + 1];
            const float denom = 2.0f * (2.0f * y2 - y1 - y3);
            const float delta = (std::abs(denom) > 1e-5f) ? ((y1 - y3) / denom) : 0.0f;
            const float best_r = sample_radii[max_idx] + delta * 0.25f;

            sub_edge_points.emplace_back(
                local_cx + best_r * cos_a + rx0,
                local_cy + best_r * sin_a + ry0
            );
        }
    }

    if (sub_edge_points.size() < 15) {
        dot_center = Point2D(rough_center);
        dot_radius = 26.5;
        return Status::OK();
    }

    double fit_cx = 0.0;
    double fit_cy = 0.0;
    double fit_r = 0.0;
    if (fitCircleTaubin(sub_edge_points, fit_cx, fit_cy, fit_r)) {
        dot_center = Point2D(fit_cx, fit_cy);
        dot_radius = fit_r;
    } else {
        dot_center = Point2D(rough_center);
        dot_radius = 26.5;
    }

    return Status::OK();
}

Status AxisPixelCalibrationModule::calibratePoints(
    const std::vector<cv::Mat>& mono8_images,
    const std::vector<Point2D>& axis_pts,
    const std::string& objective_id,
    AxisPixelCalibResult& result,
    cv::Mat& diagnostic_bgr,
    int diag_w,
    int diag_h
) {
    if (mono8_images.size() != 9) {
        return Status::Error(ErrorCode::InvalidParam, "9点标定必须输入恰好 9 张图像");
    }
    if (axis_pts.size() != 9) {
        return Status::Error(ErrorCode::InvalidParam, "9点标定必须提供恰好 9 个物理轴坐标");
    }

    result = AxisPixelCalibResult();
    result.timestamp = getCurrentTimeString();
    result.objective_id = objective_id;

    // 1. 提取每张图像的小圆亚像素坐标
    std::vector<cv::Point2d> img_cv_pts;
    img_cv_pts.reserve(9);
    std::vector<cv::Point2d> axis_cv_pts;
    axis_cv_pts.reserve(9);

    result.points.resize(9);
    for (int i = 0; i < 9; ++i) {
        Point2D center;
        double radius = 0.0;
        Status st = detectMarkerDot(mono8_images[i], center, radius);
        if (!st.ok()) {
            return Status::Error(st.code, "提取第 " + std::to_string(i) + " 张图特征失败: " + st.message);
        }

        result.points[i].index = i;
        result.points[i].pixel = center;
        result.points[i].axis = axis_pts[i];
        result.points[i].dot_radius = radius;

        img_cv_pts.emplace_back(center.x, center.y);
        axis_cv_pts.emplace_back(axis_pts[i].x, axis_pts[i].y);
    }

    // 2. 最小二乘求解 2x3 仿射变换矩阵
    // [X; Y] = M * [u; v; 1]
    cv::Mat inlier_mask;
    cv::Mat M = cv::estimateAffine2D(img_cv_pts, axis_cv_pts, inlier_mask, cv::LMEDS);
    if (M.empty() || M.rows != 2 || M.cols != 3) {
        // 若 estimateAffine2D 返回空，采用标准最小二乘 SVD 求解
        cv::Mat A(18, 6, CV_64F);
        cv::Mat B(18, 1, CV_64F);
        for (int i = 0; i < 9; ++i) {
            const double u = img_cv_pts[i].x;
            const double v = img_cv_pts[i].y;
            const double x = axis_cv_pts[i].x;
            const double y = axis_cv_pts[i].y;

            A.at<double>(2 * i, 0) = u;
            A.at<double>(2 * i, 1) = v;
            A.at<double>(2 * i, 2) = 1.0;
            A.at<double>(2 * i, 3) = 0.0;
            A.at<double>(2 * i, 4) = 0.0;
            A.at<double>(2 * i, 5) = 0.0;
            B.at<double>(2 * i, 0) = x;

            A.at<double>(2 * i + 1, 0) = 0.0;
            A.at<double>(2 * i + 1, 1) = 0.0;
            A.at<double>(2 * i + 1, 2) = 0.0;
            A.at<double>(2 * i + 1, 3) = u;
            A.at<double>(2 * i + 1, 4) = v;
            A.at<double>(2 * i + 1, 5) = 1.0;
            B.at<double>(2 * i + 1, 0) = y;
        }
        cv::Mat X;
        if (!cv::solve(A, B, X, cv::DECOMP_SVD)) {
            return Status::Error(ErrorCode::FittingFailed, "仿射矩阵最小二乘拟合求解失败");
        }
        M = cv::Mat(2, 3, CV_64F);
        M.at<double>(0, 0) = X.at<double>(0, 0);
        M.at<double>(0, 1) = X.at<double>(1, 0);
        M.at<double>(0, 2) = X.at<double>(2, 0);
        M.at<double>(1, 0) = X.at<double>(3, 0);
        M.at<double>(1, 1) = X.at<double>(4, 0);
        M.at<double>(1, 2) = X.at<double>(5, 0);
    }

    result.m_pix2axis = M.clone();

    // 3. 求解逆矩阵 (轴坐标 -> 像素)
    cv::Mat M_inv;
    cv::invertAffineTransform(M, M_inv);
    result.m_axis2pix = M_inv.clone();

    // 4. 物理光学与机械指标精密分解
    const double m00 = M.at<double>(0, 0);
    const double m01 = M.at<double>(0, 1);
    const double m10 = M.at<double>(1, 0);
    const double m11 = M.at<double>(1, 1);

    // 物理分辨率 (输入轴坐标为 mm，换算为 um/px 乘以 1000.0)
    result.pixel_scale_x_um = std::sqrt(m00 * m00 + m10 * m10) * 1000.0;
    result.pixel_scale_y_um = std::sqrt(m01 * m01 + m11 * m11) * 1000.0;
    result.pixel_scale_mean_um = (result.pixel_scale_x_um + result.pixel_scale_y_um) * 0.5;
    result.aspect_ratio = (result.pixel_scale_y_um > 1e-9) ? (result.pixel_scale_x_um / result.pixel_scale_y_um) : 1.0;

    // 安装旋转偏角 (度)
    result.rotation_angle_deg = std::atan2(m10, m00) * (180.0 / CV_PI);

    // 正交性剪切偏角 (度)
    const double theta_y_deg = std::atan2(m11, m01) * (180.0 / CV_PI);
    result.orthogonality_skew_deg = std::abs(theta_y_deg - result.rotation_angle_deg) - 90.0;

    // 5. 计算重投影残差
    double sum_sq_um = 0.0;
    double max_err_um = 0.0;
    double sum_sq_px = 0.0;
    double max_err_px = 0.0;

    const double inv00 = M_inv.at<double>(0, 0);
    const double inv01 = M_inv.at<double>(0, 1);
    const double inv02 = M_inv.at<double>(0, 2);
    const double inv10 = M_inv.at<double>(1, 0);
    const double inv11 = M_inv.at<double>(1, 1);
    const double inv12 = M_inv.at<double>(1, 2);

    for (int i = 0; i < 9; ++i) {
        const double u = result.points[i].pixel.x;
        const double v = result.points[i].pixel.y;
        const double x = result.points[i].axis.x;
        const double y = result.points[i].axis.y;

        // 物理残差 (mm -> um)
        const double pred_x = m00 * u + m01 * v + M.at<double>(0, 2);
        const double pred_y = m10 * u + m11 * v + M.at<double>(1, 2);
        const double dx_mm = pred_x - x;
        const double dy_mm = pred_y - y;
        const double err_um = std::sqrt(dx_mm * dx_mm + dy_mm * dy_mm) * 1000.0;

        result.points[i].residual_um = err_um;
        sum_sq_um += err_um * err_um;
        if (err_um > max_err_um) {
            max_err_um = err_um;
        }

        // 像面残差 (px)
        const double pred_u = inv00 * x + inv01 * y + inv02;
        const double pred_v = inv10 * x + inv11 * y + inv12;
        const double du_px = pred_u - u;
        const double dv_px = pred_v - v;
        const double err_px = std::sqrt(du_px * du_px + dv_px * dv_px);

        result.points[i].residual_px = err_px;
        sum_sq_px += err_px * err_px;
        if (err_px > max_err_px) {
            max_err_px = err_px;
        }
    }

    result.rms_residual_um = std::sqrt(sum_sq_um / 9.0);
    result.max_residual_um = max_err_um;
    result.rms_residual_px = std::sqrt(sum_sq_px / 9.0);
    result.max_residual_px = max_err_px;

    // 6. 渲染诊断大图
    renderDiagnostic(mono8_images, result, diagnostic_bgr, diag_w, diag_h);

    return Status::OK();
}

Status AxisPixelCalibrationModule::calibrateGrid(
    const std::vector<cv::Mat>& mono8_images,
    const AxisPixelGridConfig& config,
    AxisPixelCalibResult& result,
    cv::Mat& diagnostic_bgr,
    int diag_w,
    int diag_h
) {
    if (mono8_images.size() != 9) {
        return Status::Error(ErrorCode::InvalidParam, "9点标定必须输入恰好 9 张图像");
    }
    if (config.step_size_mm <= 0.0) {
        return Status::Error(ErrorCode::InvalidParam, "标定步长 step_size_mm 必须大于 0");
    }

    // 根据推导出的拓扑走位轨迹生成 9 点物理轴坐标:
    // Mark_0: (0, 0)
    // Mark_1: (-1, 0)  正西
    // Mark_2: (-1, +1) 西南
    // Mark_3: (0, +1)  正南
    // Mark_4: (+1, +1) 东南
    // Mark_5: (+1, 0)  正东
    // Mark_6: (+1, -1) 东北
    // Mark_7: (0, -1)  正北
    // Mark_8: (-1, -1) 西北
    static const int grid_dx[9] = {  0, -1, -1,  0, +1, +1, +1,  0, -1 };
    static const int grid_dy[9] = {  0,  0, +1, +1, +1,  0, -1, -1, -1 };

    std::vector<Point2D> axis_pts(9);
    for (int i = 0; i < 9; ++i) {
        const double ax = config.initial_axis_x + grid_dx[i] * config.step_size_mm * config.axis_direction_x;
        const double ay = config.initial_axis_y + grid_dy[i] * config.step_size_mm * config.axis_direction_y;
        axis_pts[i] = Point2D(ax, ay);
    }

    Status st = calibratePoints(mono8_images, axis_pts, config.objective_id, result, diagnostic_bgr, diag_w, diag_h);
    if (!st.ok()) {
        return st;
    }

    result.initial_axis_x = config.initial_axis_x;
    result.initial_axis_y = config.initial_axis_y;
    result.step_size_mm = config.step_size_mm;

    return Status::OK();
}

Point2D AxisPixelCalibrationModule::transformPixelToAxis(
    const AxisPixelCalibResult& calib,
    const Point2D& pixel_pt
) {
    if (calib.m_pix2axis.empty() || calib.m_pix2axis.rows != 2 || calib.m_pix2axis.cols != 3) {
        return Point2D(0.0, 0.0);
    }
    const double m00 = calib.m_pix2axis.at<double>(0, 0);
    const double m01 = calib.m_pix2axis.at<double>(0, 1);
    const double m02 = calib.m_pix2axis.at<double>(0, 2);
    const double m10 = calib.m_pix2axis.at<double>(1, 0);
    const double m11 = calib.m_pix2axis.at<double>(1, 1);
    const double m12 = calib.m_pix2axis.at<double>(1, 2);

    const double x = m00 * pixel_pt.x + m01 * pixel_pt.y + m02;
    const double y = m10 * pixel_pt.x + m11 * pixel_pt.y + m12;
    return Point2D(x, y);
}

Point2D AxisPixelCalibrationModule::transformAxisToPixel(
    const AxisPixelCalibResult& calib,
    const Point2D& axis_pt
) {
    if (calib.m_axis2pix.empty() || calib.m_axis2pix.rows != 2 || calib.m_axis2pix.cols != 3) {
        return Point2D(0.0, 0.0);
    }
    const double inv00 = calib.m_axis2pix.at<double>(0, 0);
    const double inv01 = calib.m_axis2pix.at<double>(0, 1);
    const double inv02 = calib.m_axis2pix.at<double>(0, 2);
    const double inv10 = calib.m_axis2pix.at<double>(1, 0);
    const double inv11 = calib.m_axis2pix.at<double>(1, 1);
    const double inv12 = calib.m_axis2pix.at<double>(1, 2);

    const double u = inv00 * axis_pt.x + inv01 * axis_pt.y + inv02;
    const double v = inv10 * axis_pt.x + inv11 * axis_pt.y + inv12;
    return Point2D(u, v);
}

Status AxisPixelCalibrationModule::saveCalibrationJson(
    const std::string& filepath,
    const AxisPixelCalibResult& result
) {
    if (filepath.empty()) {
        return Status::Error(ErrorCode::InvalidParam, "保存文件路径为空");
    }
    if (result.m_pix2axis.empty() || result.m_axis2pix.empty()) {
        return Status::Error(ErrorCode::InvalidParam, "标定结果矩阵为空，无法保存");
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8);
    oss << "{\n";
    oss << "  \"version\": \"" << result.version << "\",\n";
    oss << "  \"calibration_type\": \"AxisPixel_9Point\",\n";
    oss << "  \"timestamp\": \"" << result.timestamp << "\",\n";
    oss << "  \"objective_id\": \"" << result.objective_id << "\",\n";

    oss << "  \"nominal_parameters\": {\n";
    oss << "    \"initial_axis_x\": " << result.initial_axis_x << ",\n";
    oss << "    \"initial_axis_y\": " << result.initial_axis_y << ",\n";
    oss << "    \"step_size_mm\": " << result.step_size_mm << "\n";
    oss << "  },\n";

    oss << "  \"physical_metrics\": {\n";
    oss << "    \"pixel_scale_x_um\": " << result.pixel_scale_x_um << ",\n";
    oss << "    \"pixel_scale_y_um\": " << result.pixel_scale_y_um << ",\n";
    oss << "    \"pixel_scale_mean_um\": " << result.pixel_scale_mean_um << ",\n";
    oss << "    \"aspect_ratio\": " << result.aspect_ratio << ",\n";
    oss << "    \"rotation_angle_deg\": " << result.rotation_angle_deg << ",\n";
    oss << "    \"orthogonality_skew_deg\": " << result.orthogonality_skew_deg << "\n";
    oss << "  },\n";

    oss << "  \"accuracy_metrics\": {\n";
    oss << "    \"rms_reprojection_error_um\": " << result.rms_residual_um << ",\n";
    oss << "    \"max_reprojection_error_um\": " << result.max_residual_um << ",\n";
    oss << "    \"rms_reprojection_error_px\": " << result.rms_residual_px << ",\n";
    oss << "    \"max_reprojection_error_px\": " << result.max_residual_px << "\n";
    oss << "  },\n";

    auto formatMatVal = [](double v) -> std::string {
        std::ostringstream ss;
        ss << std::scientific << std::setprecision(16) << v;
        return ss.str();
    };

    oss << "  \"matrices\": {\n";
    oss << "    \"pixel_to_axis\": [\n";
    oss << "      [" << formatMatVal(result.m_pix2axis.at<double>(0, 0)) << ", " << formatMatVal(result.m_pix2axis.at<double>(0, 1)) << ", " << formatMatVal(result.m_pix2axis.at<double>(0, 2)) << "],\n";
    oss << "      [" << formatMatVal(result.m_pix2axis.at<double>(1, 0)) << ", " << formatMatVal(result.m_pix2axis.at<double>(1, 1)) << ", " << formatMatVal(result.m_pix2axis.at<double>(1, 2)) << "]\n";
    oss << "    ],\n";
    oss << "    \"axis_to_pixel\": [\n";
    oss << "      [" << formatMatVal(result.m_axis2pix.at<double>(0, 0)) << ", " << formatMatVal(result.m_axis2pix.at<double>(0, 1)) << ", " << formatMatVal(result.m_axis2pix.at<double>(0, 2)) << "],\n";
    oss << "      [" << formatMatVal(result.m_axis2pix.at<double>(1, 0)) << ", " << formatMatVal(result.m_axis2pix.at<double>(1, 1)) << ", " << formatMatVal(result.m_axis2pix.at<double>(1, 2)) << "]\n";
    oss << "    ]\n";
    oss << "  },\n";

    oss << "  \"calibrated_points\": [\n";
    for (size_t i = 0; i < result.points.size(); ++i) {
        const auto& p = result.points[i];
        oss << "    {\n";
        oss << "      \"index\": " << p.index << ",\n";
        oss << "      \"pixel\": [" << p.pixel.x << ", " << p.pixel.y << "],\n";
        oss << "      \"axis\": [" << p.axis.x << ", " << p.axis.y << "],\n";
        oss << "      \"dot_radius_px\": " << p.dot_radius << ",\n";
        oss << "      \"residual_um\": " << p.residual_um << ",\n";
        oss << "      \"residual_px\": " << p.residual_px << "\n";
        oss << "    }" << (i + 1 < result.points.size() ? "," : "") << "\n";
    }
    oss << "  ]\n";
    oss << "}\n";

    std::string json_str = oss.str();

#ifdef _WIN32
    std::wstring wpath = stringToWstring(filepath);
    std::ofstream ofs(wpath, std::ios::out | std::ios::binary);
#else
    std::ofstream ofs(filepath, std::ios::out | std::ios::binary);
#endif

    if (!ofs.is_open()) {
        return Status::Error(ErrorCode::FileIOError, "无法打开文件以写入 JSON: " + filepath);
    }

    ofs.write(json_str.data(), json_str.size());
    ofs.close();

    return Status::OK();
}

Status AxisPixelCalibrationModule::loadCalibrationJson(
    const std::string& filepath,
    AxisPixelCalibResult& result
) {
    if (filepath.empty()) {
        return Status::Error(ErrorCode::InvalidParam, "文件路径为空");
    }

#ifdef _WIN32
    std::wstring wpath = stringToWstring(filepath);
    std::ifstream ifs(wpath, std::ios::in | std::ios::binary);
#else
    std::ifstream ifs(filepath, std::ios::in | std::ios::binary);
#endif

    if (!ifs.is_open()) {
        return Status::Error(ErrorCode::FileIOError, "无法打开标定配方文件: " + filepath);
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string content = buffer.str();
    ifs.close();

    if (content.empty()) {
        return Status::Error(ErrorCode::FileIOError, "标定配方文件内容为空");
    }

    // 辅助简易 JSON 数值提取函数
    auto findNumber = [&](const std::string& key, double default_val = 0.0) -> double {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return default_val;
        size_t colon = content.find(':', pos);
        if (colon == std::string::npos) return default_val;
        size_t start = content.find_first_not_of(" \t\r\n", colon + 1);
        if (start == std::string::npos) return default_val;
        try {
            return std::stod(content.substr(start));
        } catch (...) {
            return default_val;
        }
    };

    auto findString = [&](const std::string& key) -> std::string {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        size_t colon = content.find(':', pos);
        if (colon == std::string::npos) return "";
        size_t q1 = content.find('\"', colon + 1);
        if (q1 == std::string::npos) return "";
        size_t q2 = content.find('\"', q1 + 1);
        if (q2 == std::string::npos) return "";
        return content.substr(q1 + 1, q2 - q1 - 1);
    };

    result = AxisPixelCalibResult();
    result.version = findString("version");
    result.timestamp = findString("timestamp");
    result.objective_id = findString("objective_id");

    result.initial_axis_x = findNumber("initial_axis_x");
    result.initial_axis_y = findNumber("initial_axis_y");
    result.step_size_mm = findNumber("step_size_mm");

    result.pixel_scale_x_um = findNumber("pixel_scale_x_um");
    result.pixel_scale_y_um = findNumber("pixel_scale_y_um");
    result.pixel_scale_mean_um = findNumber("pixel_scale_mean_um");
    result.aspect_ratio = findNumber("aspect_ratio", 1.0);
    result.rotation_angle_deg = findNumber("rotation_angle_deg");
    result.orthogonality_skew_deg = findNumber("orthogonality_skew_deg");

    result.rms_residual_um = findNumber("rms_reprojection_error_um");
    result.max_residual_um = findNumber("max_reprojection_error_um");
    result.rms_residual_px = findNumber("rms_reprojection_error_px");
    result.max_residual_px = findNumber("max_reprojection_error_px");

    // 解析 2x3 pixel_to_axis 矩阵
    size_t p2a_pos = content.find("\"pixel_to_axis\"");
    if (p2a_pos != std::string::npos) {
        size_t arr_start = content.find('[', p2a_pos);
        if (arr_start != std::string::npos) {
            std::vector<double> vals;
            size_t idx = arr_start;
            while (vals.size() < 6 && idx < content.size()) {
                if ((content[idx] >= '0' && content[idx] <= '9') || content[idx] == '-' || content[idx] == '+') {
                    char* end_ptr = nullptr;
                    double v = std::strtod(&content[idx], &end_ptr);
                    if (end_ptr != &content[idx]) {
                        vals.push_back(v);
                        idx += (end_ptr - &content[idx]);
                        continue;
                    }
                }
                ++idx;
            }
            if (vals.size() == 6) {
                result.m_pix2axis = cv::Mat(2, 3, CV_64F);
                result.m_pix2axis.at<double>(0, 0) = vals[0];
                result.m_pix2axis.at<double>(0, 1) = vals[1];
                result.m_pix2axis.at<double>(0, 2) = vals[2];
                result.m_pix2axis.at<double>(1, 0) = vals[3];
                result.m_pix2axis.at<double>(1, 1) = vals[4];
                result.m_pix2axis.at<double>(1, 2) = vals[5];
            }
        }
    }

    // 解析 2x3 axis_to_pixel 矩阵
    size_t a2p_pos = content.find("\"axis_to_pixel\"");
    if (a2p_pos != std::string::npos) {
        size_t arr_start = content.find('[', a2p_pos);
        if (arr_start != std::string::npos) {
            std::vector<double> vals;
            size_t idx = arr_start;
            while (vals.size() < 6 && idx < content.size()) {
                if ((content[idx] >= '0' && content[idx] <= '9') || content[idx] == '-' || content[idx] == '+') {
                    char* end_ptr = nullptr;
                    double v = std::strtod(&content[idx], &end_ptr);
                    if (end_ptr != &content[idx]) {
                        vals.push_back(v);
                        idx += (end_ptr - &content[idx]);
                        continue;
                    }
                }
                ++idx;
            }
            if (vals.size() == 6) {
                result.m_axis2pix = cv::Mat(2, 3, CV_64F);
                result.m_axis2pix.at<double>(0, 0) = vals[0];
                result.m_axis2pix.at<double>(0, 1) = vals[1];
                result.m_axis2pix.at<double>(0, 2) = vals[2];
                result.m_axis2pix.at<double>(1, 0) = vals[3];
                result.m_axis2pix.at<double>(1, 1) = vals[4];
                result.m_axis2pix.at<double>(1, 2) = vals[5];
            }
        }
    }

    if (result.m_pix2axis.empty()) {
        return Status::Error(ErrorCode::FileIOError, "未能从 JSON 文件中解析出有效的 pixel_to_axis 变换矩阵");
    }

    if (result.m_axis2pix.empty()) {
        cv::invertAffineTransform(result.m_pix2axis, result.m_axis2pix);
    }

    return Status::OK();
}

void AxisPixelCalibrationModule::renderDiagnostic(
    const std::vector<cv::Mat>& mono8_images,
    const AxisPixelCalibResult& result,
    cv::Mat& out_diag_bgr,
    int diag_w,
    int diag_h
) {
    out_diag_bgr.create(diag_h, diag_w, CV_8UC3);
    out_diag_bgr.setTo(cv::Scalar(22, 26, 34)); // 深灰色工业看板背景

    const int header_h = 100;
    const cv::Scalar col_title(255, 255, 255);
    const cv::Scalar col_accent(0, 195, 255);      // 亮黄/橙高亮
    const cv::Scalar col_green(80, 220, 100);     // 成功绿色
    const cv::Scalar col_card_bg(32, 38, 50);     // 卡片背景
    const cv::Scalar col_card_border(55, 65, 85); // 卡片边框
    const cv::Scalar col_text(220, 225, 235);
    const cv::Scalar col_subtext(140, 150, 170);

    // 1. 顶部标题栏
    cv::rectangle(out_diag_bgr, cv::Rect(0, 0, diag_w, header_h), cv::Scalar(16, 20, 26), -1);
    cv::line(out_diag_bgr, cv::Point(0, header_h), cv::Point(diag_w, header_h), col_accent, 2);

    cv::putText(out_diag_bgr, "WAFER CHUCK AXIS-PIXEL 9-POINT CALIBRATION REPORT", cv::Point(50, 50),
                cv::FONT_HERSHEY_DUPLEX, 1.1, col_title, 2, cv::LINE_AA);
    cv::putText(out_diag_bgr, "WaferCalibSDK High-Precision Vision-Motion Hand-Eye Calibration Diagnostic View",
                cv::Point(50, 80), cv::FONT_HERSHEY_PLAIN, 1.2, col_subtext, 1, cv::LINE_AA);

    std::string time_str = "Timestamp: " + (result.timestamp.empty() ? getCurrentTimeString() : result.timestamp);
    cv::putText(out_diag_bgr, time_str, cv::Point(diag_w - 500, 55),
                cv::FONT_HERSHEY_PLAIN, 1.2, col_accent, 1, cv::LINE_AA);

    // 布局划分:
    // 左侧: 3x3 视场微标特写与闭环轨迹图 (宽度 1550)
    // 右上: 重投影残差矢量图 (宽度 1300, 高度 950)
    // 右下: 精密参数指标与残差明细看板 (宽度 1300, 高度 1200)
    const int margin = 40;
    const int left_x = margin;
    const int left_y = header_h + margin;
    const int left_w = 1520;
    const int left_h = diag_h - header_h - margin * 2;

    const int right_x = left_x + left_w + margin;
    const int right_w = diag_w - right_x - margin;
    const int right_top_y = left_y;
    const int right_top_h = 950;
    const int right_bot_y = right_top_y + right_top_h + margin;
    const int right_bot_h = diag_h - right_bot_y - margin;

    // ==========================================
    // 左侧模块: 3x3 视场特写与走位轨迹图
    // ==========================================
    cv::rectangle(out_diag_bgr, cv::Rect(left_x, left_y, left_w, left_h), col_card_bg, -1);
    cv::rectangle(out_diag_bgr, cv::Rect(left_x, left_y, left_w, left_h), col_card_border, 2);

    cv::putText(out_diag_bgr, "3x3 GRID PHYSICAL FOV & TRAJECTORY RESTORATION", cv::Point(left_x + 30, left_y + 45),
                cv::FONT_HERSHEY_DUPLEX, 0.9, col_accent, 2, cv::LINE_AA);
    cv::putText(out_diag_bgr, "Sub-pixel feature dots detected at 9 motion positions, connected by perimeter trajectory loop (0 -> 1 -> ... -> 8)",
                cv::Point(left_x + 30, left_y + 75), cv::FONT_HERSHEY_PLAIN, 1.1, col_subtext, 1, cv::LINE_AA);

    // 3x3 宫格在左侧面板内的放置
    // 映射关系:
    // Row 0: Mark_8 (-1,-1), Mark_7 (0,-1), Mark_6 (+1,-1)
    // Row 1: Mark_1 (-1, 0), Mark_0 (0, 0), Mark_5 (+1, 0)
    // Row 2: Mark_2 (-1,+1), Mark_3 (0,+1), Mark_4 (+1,+1)
    static const int grid_img_idx[3][3] = {
        { 8, 7, 6 },
        { 1, 0, 5 },
        { 2, 3, 4 }
    };

    const int grid_margin_top = 100;
    const int cell_size = (left_w - 80) / 3;
    const int cell_inner = cell_size - 20;

    std::vector<cv::Point> cell_centers(9);

    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const int mark_id = grid_img_idx[r][c];
            const int cx = left_x + 40 + c * cell_size;
            const int cy = left_y + grid_margin_top + r * cell_size;

            cell_centers[mark_id] = cv::Point(cx + cell_inner / 2, cy + cell_inner / 2);

            // 绘制单元格背景框
            cv::rectangle(out_diag_bgr, cv::Rect(cx, cy, cell_inner, cell_inner), cv::Scalar(20, 24, 32), -1);
            cv::rectangle(out_diag_bgr, cv::Rect(cx, cy, cell_inner, cell_inner),
                          (mark_id == 0) ? col_accent : col_card_border, (mark_id == 0) ? 2 : 1);

            // 裁剪特写区域 (200x200 像素 ROI)
            if (mark_id < static_cast<int>(mono8_images.size()) && !mono8_images[mark_id].empty() &&
                mark_id < static_cast<int>(result.points.size())) {
                const auto& pt = result.points[mark_id];
                const int roi_sz = 160;
                const int rx = std::clamp(static_cast<int>(std::round(pt.pixel.x - roi_sz / 2)), 0, mono8_images[mark_id].cols - roi_sz);
                const int ry = std::clamp(static_cast<int>(std::round(pt.pixel.y - roi_sz / 2)), 0, mono8_images[mark_id].rows - roi_sz);

                cv::Mat crop_m8 = mono8_images[mark_id](cv::Rect(rx, ry, roi_sz, roi_sz));
                cv::Mat crop_bgr;
                cv::cvtColor(crop_m8, crop_bgr, cv::COLOR_GRAY2BGR);

                // 在 crop 上绘制检测圆和十字线
                const double local_u = pt.pixel.x - rx;
                const double local_v = pt.pixel.y - ry;
                cv::circle(crop_bgr, cv::Point2d(local_u, local_v), static_cast<int>(std::round(pt.dot_radius)), cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
                cv::drawMarker(crop_bgr, cv::Point2d(local_u, local_v), cv::Scalar(0, 0, 255), cv::MARKER_CROSS, 20, 1, cv::LINE_AA);

                // 缩放到 cell 尺寸
                cv::Mat cell_vis;
                cv::resize(crop_bgr, cell_vis, cv::Size(cell_inner, cell_inner));
                cell_vis.copyTo(out_diag_bgr(cv::Rect(cx, cy, cell_inner, cell_inner)));
            }

            // 叠加文字标头 (如 Mark_0 (Center))
            std::string pos_tag;
            switch (mark_id) {
                case 0: pos_tag = "Mark_0 [Center]"; break;
                case 1: pos_tag = "Mark_1 [West]"; break;
                case 2: pos_tag = "Mark_2 [South-West]"; break;
                case 3: pos_tag = "Mark_3 [South]"; break;
                case 4: pos_tag = "Mark_4 [South-East]"; break;
                case 5: pos_tag = "Mark_5 [East]"; break;
                case 6: pos_tag = "Mark_6 [North-East]"; break;
                case 7: pos_tag = "Mark_7 [North]"; break;
                case 8: pos_tag = "Mark_8 [North-West]"; break;
                default: break;
            }

            cv::rectangle(out_diag_bgr, cv::Rect(cx, cy, cell_inner, 32), cv::Scalar(10, 14, 20), -1);
            cv::putText(out_diag_bgr, pos_tag, cv::Point(cx + 10, cy + 22),
                        cv::FONT_HERSHEY_DUPLEX, 0.6, (mark_id == 0) ? col_accent : cv::Scalar(240, 240, 240), 1, cv::LINE_AA);

            // 叠加像素坐标
            if (mark_id < static_cast<int>(result.points.size())) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1) << "u=" << result.points[mark_id].pixel.x << " v=" << result.points[mark_id].pixel.y;
                cv::putText(out_diag_bgr, oss.str(), cv::Point(cx + 10, cy + cell_inner - 12),
                            cv::FONT_HERSHEY_PLAIN, 1.1, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
            }
        }
    }

    // 绘制外周走位黄色引导轨迹箭头 0 -> 1 -> 2 -> ... -> 8
    static const int traj_order[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    for (int step = 0; step < 8; ++step) {
        const int from_id = traj_order[step];
        const int to_id = traj_order[step + 1];
        const cv::Point p1 = cell_centers[from_id];
        const cv::Point p2 = cell_centers[to_id];

        // 绘制粗箭头与步骤序号
        cv::arrowedLine(out_diag_bgr, p1, p2, cv::Scalar(0, 220, 255), 4, cv::LINE_AA, 0, 0.08);

        cv::Point mid = (p1 + p2) * 0.5;
        cv::circle(out_diag_bgr, mid, 16, cv::Scalar(20, 24, 32), -1);
        cv::circle(out_diag_bgr, mid, 16, cv::Scalar(0, 220, 255), 2);
        std::string step_str = std::to_string(step + 1);
        cv::putText(out_diag_bgr, step_str, cv::Point(mid.x - 6, mid.y + 6),
                    cv::FONT_HERSHEY_DUPLEX, 0.6, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
    }

    // ==========================================
    // 右上模块: 重投影残差矢量放大图
    // ==========================================
    cv::rectangle(out_diag_bgr, cv::Rect(right_x, right_top_y, right_w, right_top_h), col_card_bg, -1);
    cv::rectangle(out_diag_bgr, cv::Rect(right_x, right_top_y, right_w, right_top_h), col_card_border, 2);

    cv::putText(out_diag_bgr, "REPROJECTION RESIDUAL VECTOR MAP (MAGNIFIED 500x)", cv::Point(right_x + 30, right_top_y + 45),
                cv::FONT_HERSHEY_DUPLEX, 0.85, col_accent, 2, cv::LINE_AA);
    cv::putText(out_diag_bgr, "Black Cross: Nominal Grid Position | Green Circle: Reprojected Position | Red Arrow: Error Vector (x500)",
                cv::Point(right_x + 30, right_top_y + 75), cv::FONT_HERSHEY_PLAIN, 1.1, col_subtext, 1, cv::LINE_AA);

    // 网格残差图中心与比例
    const int map_cx = right_x + right_w / 2;
    const int map_cy = right_top_y + (right_top_h + 80) / 2;
    const double map_scale = 320.0; // 320 像素对应 1 个网格步长

    // 绘制参考网格线
    for (int k = -1; k <= 1; ++k) {
        cv::line(out_diag_bgr,
                 cv::Point(map_cx - map_scale * 1.3, map_cy + k * map_scale),
                 cv::Point(map_cx + map_scale * 1.3, map_cy + k * map_scale),
                 cv::Scalar(45, 55, 70), 1, cv::LINE_AA);
        cv::line(out_diag_bgr,
                 cv::Point(map_cx + k * map_scale, map_cy - map_scale * 1.3),
                 cv::Point(map_cx + k * map_scale, map_cy + map_scale * 1.3),
                 cv::Scalar(45, 55, 70), 1, cv::LINE_AA);
    }

    static const int grid_k_x[9] = {  0, -1, -1,  0, +1, +1, +1,  0, -1 };
    static const int grid_k_y[9] = {  0,  0, +1, +1, +1,  0, -1, -1, -1 };

    for (int i = 0; i < 9 && i < static_cast<int>(result.points.size()); ++i) {
        const double gx = map_cx + grid_k_x[i] * map_scale;
        const double gy = map_cy + grid_k_y[i] * map_scale;
        const cv::Point nom_pt(static_cast<int>(std::round(gx)), static_cast<int>(std::round(gy)));

        // 绘制理想网格黑色/灰色十字标
        cv::drawMarker(out_diag_bgr, nom_pt, cv::Scalar(180, 190, 200), cv::MARKER_CROSS, 24, 2, cv::LINE_AA);

        // 获取实际残差并放大 500 倍 (以像素为单位显示)
        // 从实际像素点反投得到的偏移
        const double res_um = result.points[i].residual_um;
        const double arrow_len = std::max(8.0, res_um * 25.0); // 视觉适度放大

        // 根据真实检测点在轴坐标与仿射拟合之间的偏差确定方向
        const cv::Point2d actual_axis = result.points[i].axis.toCvPoint();
        const cv::Point2d pred_axis = AxisPixelCalibrationModule::transformPixelToAxis(result, result.points[i].pixel).toCvPoint();
        const double d_ax = pred_axis.x - actual_axis.x;
        const double d_ay = pred_axis.y - actual_axis.y;
        double angle_rad = std::atan2(d_ay, d_ax);

        cv::Point arrow_end(
            static_cast<int>(std::round(nom_pt.x + arrow_len * std::cos(angle_rad))),
            static_cast<int>(std::round(nom_pt.y + arrow_len * std::sin(angle_rad)))
        );

        // 绘制反推位置绿色小圆
        cv::circle(out_diag_bgr, arrow_end, 5, col_green, -1, cv::LINE_AA);

        // 绘制红色误差矢量箭头
        cv::arrowedLine(out_diag_bgr, nom_pt, arrow_end, cv::Scalar(0, 60, 255), 2, cv::LINE_AA, 0, 0.25);

        // 标注残差数值
        std::ostringstream res_oss;
        res_oss << std::fixed << std::setprecision(2) << res_um << " um";
        cv::putText(out_diag_bgr, res_oss.str(), cv::Point(nom_pt.x + 10, nom_pt.y - 10),
                    cv::FONT_HERSHEY_PLAIN, 1.1, cv::Scalar(255, 120, 120), 1, cv::LINE_AA);
    }

    // ==========================================
    // 右下模块: 精密参数指标与精度评估看板
    // ==========================================
    cv::rectangle(out_diag_bgr, cv::Rect(right_x, right_bot_y, right_w, right_bot_h), col_card_bg, -1);
    cv::rectangle(out_diag_bgr, cv::Rect(right_x, right_bot_y, right_w, right_bot_h), col_card_border, 2);

    cv::putText(out_diag_bgr, "CALIBRATION METRICS & ERROR EVALUATION DASHBOARD", cv::Point(right_x + 30, right_bot_y + 40),
                cv::FONT_HERSHEY_DUPLEX, 0.85, col_accent, 2, cv::LINE_AA);

    int text_y = right_bot_y + 80;
    const int col1_x = right_x + 40;
    const int col2_x = right_x + right_w / 2 + 20;

    auto drawMetricColor = [&](const std::string& label, const std::string& val, int x, int y, const cv::Scalar& val_col) {
        cv::putText(out_diag_bgr, label, cv::Point(x, y), cv::FONT_HERSHEY_DUPLEX, 0.65, col_text, 1, cv::LINE_AA);
        cv::putText(out_diag_bgr, val, cv::Point(x + 340, y), cv::FONT_HERSHEY_DUPLEX, 0.7, val_col, 2, cv::LINE_AA);
    };
    auto drawMetric = [&](const std::string& label, const std::string& val, int x, int y) {
        drawMetricColor(label, val, x, y, col_accent);
    };

    // 核心指标
    std::ostringstream oss;
    oss.str(""); oss << "Obj_" << result.objective_id;
    drawMetricColor("Objective ID:", oss.str(), col1_x, text_y, cv::Scalar(255, 255, 255));

    oss.str(""); oss << std::fixed << std::setprecision(4) << result.pixel_scale_x_um << " um/px";
    drawMetric("Pixel Scale X (sx):", oss.str(), col1_x, text_y + 35);

    oss.str(""); oss << std::fixed << std::setprecision(4) << result.pixel_scale_y_um << " um/px";
    drawMetric("Pixel Scale Y (sy):", oss.str(), col1_x, text_y + 70);

    oss.str(""); oss << std::fixed << std::setprecision(4) << result.pixel_scale_mean_um << " um/px";
    drawMetric("Mean Pixel Scale:", oss.str(), col1_x, text_y + 105);

    oss.str(""); oss << std::fixed << std::setprecision(5) << result.aspect_ratio;
    drawMetricColor("Aspect Ratio (sx/sy):", oss.str(), col1_x, text_y + 140, col_green);

    // 右列: 几何与误差指标
    oss.str(""); oss << std::fixed << std::setprecision(4) << result.rotation_angle_deg << " deg";
    drawMetric("Mounting Angle (theta):", oss.str(), col2_x, text_y);

    oss.str(""); oss << std::fixed << std::setprecision(4) << result.orthogonality_skew_deg << " deg";
    drawMetric("Orthogonality Skew:", oss.str(), col2_x, text_y + 35);

    oss.str(""); oss << std::fixed << std::setprecision(3) << result.rms_residual_um << " um  (" << std::setprecision(2) << result.rms_residual_px << " px)";
    drawMetricColor("Global RMS Residual:", oss.str(), col2_x, text_y + 70, col_green);

    oss.str(""); oss << std::fixed << std::setprecision(3) << result.max_residual_um << " um  (" << std::setprecision(2) << result.max_residual_px << " px)";
    drawMetricColor("Max Point Residual:", oss.str(), col2_x, text_y + 105, (result.max_residual_um < 2.5) ? col_green : cv::Scalar(0, 165, 255));

    // 分割线
    const int table_y = text_y + 170;
    cv::line(out_diag_bgr, cv::Point(col1_x, table_y), cv::Point(right_x + right_w - 40, table_y), col_card_border, 1);

    // 9点明细表格
    cv::putText(out_diag_bgr, "POINT DETAILED MAPPING & REPROJECTION RESIDUAL TABLE", cv::Point(col1_x, table_y + 30),
                cv::FONT_HERSHEY_DUPLEX, 0.7, col_title, 1, cv::LINE_AA);

    const int row_h = 24;
    int table_row_y = table_y + 60;

    // 表头
    cv::putText(out_diag_bgr, "ID", cv::Point(col1_x, table_row_y), cv::FONT_HERSHEY_PLAIN, 1.1, col_subtext, 1, cv::LINE_AA);
    cv::putText(out_diag_bgr, "Pixel (u, v)", cv::Point(col1_x + 60, table_row_y), cv::FONT_HERSHEY_PLAIN, 1.1, col_subtext, 1, cv::LINE_AA);
    cv::putText(out_diag_bgr, "Axis (X, Y) [mm]", cv::Point(col1_x + 360, table_row_y), cv::FONT_HERSHEY_PLAIN, 1.1, col_subtext, 1, cv::LINE_AA);
    cv::putText(out_diag_bgr, "Dot R (px)", cv::Point(col1_x + 720, table_row_y), cv::FONT_HERSHEY_PLAIN, 1.1, col_subtext, 1, cv::LINE_AA);
    cv::putText(out_diag_bgr, "Res (um)", cv::Point(col1_x + 880, table_row_y), cv::FONT_HERSHEY_PLAIN, 1.1, col_subtext, 1, cv::LINE_AA);
    cv::putText(out_diag_bgr, "Res (px)", cv::Point(col1_x + 1040, table_row_y), cv::FONT_HERSHEY_PLAIN, 1.1, col_subtext, 1, cv::LINE_AA);

    table_row_y += 10;
    cv::line(out_diag_bgr, cv::Point(col1_x, table_row_y), cv::Point(right_x + right_w - 40, table_row_y), col_card_border, 1);

    for (size_t i = 0; i < result.points.size(); ++i) {
        table_row_y += row_h;
        const auto& p = result.points[i];

        std::string id_str = "P" + std::to_string(p.index);
        cv::putText(out_diag_bgr, id_str, cv::Point(col1_x, table_row_y), cv::FONT_HERSHEY_PLAIN, 1.1, col_accent, 1, cv::LINE_AA);

        oss.str(""); oss << std::fixed << std::setprecision(2) << "(" << p.pixel.x << ", " << p.pixel.y << ")";
        cv::putText(out_diag_bgr, oss.str(), cv::Point(col1_x + 60, table_row_y), cv::FONT_HERSHEY_PLAIN, 1.1, col_text, 1, cv::LINE_AA);

        oss.str(""); oss << std::fixed << std::setprecision(5) << "(" << p.axis.x << ", " << p.axis.y << ")";
        cv::putText(out_diag_bgr, oss.str(), cv::Point(col1_x + 360, table_row_y), cv::FONT_HERSHEY_PLAIN, 1.1, col_text, 1, cv::LINE_AA);

        oss.str(""); oss << std::fixed << std::setprecision(2) << p.dot_radius;
        cv::putText(out_diag_bgr, oss.str(), cv::Point(col1_x + 720, table_row_y), cv::FONT_HERSHEY_PLAIN, 1.1, col_text, 1, cv::LINE_AA);

        oss.str(""); oss << std::fixed << std::setprecision(3) << p.residual_um;
        cv::putText(out_diag_bgr, oss.str(), cv::Point(col1_x + 880, table_row_y), cv::FONT_HERSHEY_PLAIN, 1.1, col_green, 1, cv::LINE_AA);

        oss.str(""); oss << std::fixed << std::setprecision(3) << p.residual_px;
        cv::putText(out_diag_bgr, oss.str(), cv::Point(col1_x + 1040, table_row_y), cv::FONT_HERSHEY_PLAIN, 1.1, col_text, 1, cv::LINE_AA);
    }
}

} // namespace wafer_calib
