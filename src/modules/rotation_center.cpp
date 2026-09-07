#include "wafer_calib/modules/rotation_center.hpp"

#include <cmath>
#include <algorithm>
#include <iomanip>
#include <sstream>

#include <opencv2/imgproc.hpp>

namespace wafer_calib {

namespace {

// Taubin 代数圆拟合封闭解
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

// 单图微标小圆亚像素中心提取
bool extractSingleMarkerDot(
    const cv::Mat& mono8,
    cv::Point2d& out_center,
    double& out_radius,
    cv::Mat* out_roi_vis = nullptr
) {
    if (mono8.empty() || mono8.type() != CV_8UC1) {
        return false;
    }

    // 1. 二值化分割 (针对工业微观低对比度图像动态自适应阈值)
    cv::Mat thresh;
    cv::threshold(mono8, thresh, 70.0, 255.0, cv::THRESH_BINARY);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    cv::Point2d rough_center;
    bool found_cand = false;
    double best_diff = 1e9;

    for (const auto& c : contours) {
        const double area = cv::contourArea(c);
        if (area < 800.0 || area > 3500.0) {
            continue; // 排除巨大十字臂与微小噪点
        }

        const cv::Rect bbox = cv::boundingRect(c);
        const double aspect = static_cast<double>(bbox.width) / std::max(1, bbox.height);
        if (aspect < 0.75 || aspect > 1.33) {
            continue; // 排除细长杂斑
        }

        const cv::Moments m = cv::moments(c);
        if (m.m00 <= 0.0) {
            continue;
        }

        const double diff = std::abs(area - 1950.0);
        if (diff < best_diff) {
            best_diff = diff;
            rough_center = cv::Point2d(m.m10 / m.m00, m.m01 / m.m00);
            found_cand = true;
        }
    }

    if (!found_cand) {
        return false;
    }

    // 2. 亚像素边缘采样
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

            // 双线性插值
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
        out_center = rough_center;
        out_radius = 26.5;
        return true;
    }

    // 3. 拟合小圆
    double fcx = 0.0;
    double fcy = 0.0;
    double frad = 0.0;
    if (fitCircleTaubin(sub_edge_points, fcx, fcy, frad)) {
        out_center = cv::Point2d(fcx, fcy);
        out_radius = frad;
    } else {
        out_center = rough_center;
        out_radius = 26.5;
    }

    // 可视化单图 ROI
    if (out_roi_vis) {
        cv::cvtColor(roi, *out_roi_vis, cv::COLOR_GRAY2BGR);
        const cv::Point center_in_roi(static_cast<int>(std::round(out_center.x - rx0)),
                                      static_cast<int>(std::round(out_center.y - ry0)));
        cv::circle(*out_roi_vis, center_in_roi, static_cast<int>(std::round(out_radius)), cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
        cv::circle(*out_roi_vis, center_in_roi, 2, cv::Scalar(0, 0, 255), -1, cv::LINE_AA);
    }

    return true;
}

} // namespace

Status PlatformRotationCenterModule::detectMarkerDotCenter(
    const cv::Mat& mono8,
    Point2D& dot_center,
    double& dot_radius
) {
    if (mono8.empty()) {
        return Status::Error(ErrorCode::ImageEmpty, "input mono8 image is empty");
    }
    if (mono8.type() != CV_8UC1) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "input image must be CV_8UC1");
    }

    cv::Point2d center;
    double radius = 0.0;
    if (!extractSingleMarkerDot(mono8, center, radius)) {
        return Status::Error(ErrorCode::FeatureNotFound, "failed to detect marker dot in image");
    }

    dot_center = Point2D(center.x, center.y);
    dot_radius = radius;
    return Status::OK();
}

Status PlatformRotationCenterModule::calculateRotationCenter(
    const std::vector<cv::Mat>& mono8_images,
    PlatformRotationCenterResult& result,
    cv::Mat& diagnostic_bgr
) {
    if (mono8_images.size() < 3) {
        return Status::Error(ErrorCode::InvalidParam, "at least 3 images are required for circle fitting");
    }

    const int width = mono8_images[0].cols;
    const int height = mono8_images[0].rows;
    for (size_t i = 0; i < mono8_images.size(); ++i) {
        if (mono8_images[i].empty()) {
            return Status::Error(ErrorCode::ImageEmpty, cv::format("image %zu is empty", i));
        }
        if (mono8_images[i].type() != CV_8UC1) {
            return Status::Error(ErrorCode::ImageFormatMismatch, cv::format("image %zu must be CV_8UC1", i));
        }
        if (mono8_images[i].cols != width || mono8_images[i].rows != height) {
            return Status::Error(ErrorCode::ImageFormatMismatch, cv::format("image %zu size mismatch", i));
        }
    }

    // 1. 提取所有图像中的微标小圆亚像素中心
    std::vector<cv::Point2d> points;
    std::vector<double> radii;
    points.reserve(mono8_images.size());
    radii.reserve(mono8_images.size());

    cv::Mat first_roi_vis;

    for (size_t i = 0; i < mono8_images.size(); ++i) {
        cv::Point2d pt;
        double r = 0.0;
        cv::Mat* p_roi = (i == 0) ? &first_roi_vis : nullptr;
        if (!extractSingleMarkerDot(mono8_images[i], pt, r, p_roi)) {
            return Status::Error(ErrorCode::FeatureNotFound, cv::format("failed to detect marker dot in image %zu", i));
        }
        points.push_back(pt);
        radii.push_back(r);
    }

    // 2. Taubin 初始大圆拟合
    double cx = 0.0;
    double cy = 0.0;
    double radius = 0.0;
    if (!fitCircleTaubin(points, cx, cy, radius)) {
        return Status::Error(ErrorCode::FittingFailed, "initial circle fitting failed");
    }

    // 3. 稳健离群点剔除 (若点数 >= 8 且存在个别粗大误差则剔除后重新拟合)
    const int n_pts = static_cast<int>(points.size());
    std::vector<double> residuals(n_pts);
    for (int i = 0; i < n_pts; ++i) {
        residuals[i] = std::abs(std::hypot(points[i].x - cx, points[i].y - cy) - radius);
    }
    std::vector<double> sorted_res = residuals;
    std::sort(sorted_res.begin(), sorted_res.end());
    const double median_res = sorted_res[n_pts / 2];
    const double outlier_thresh = std::max(10.0, 3.5 * median_res);

    std::vector<cv::Point2d> inlier_points;
    inlier_points.reserve(n_pts);
    for (int i = 0; i < n_pts; ++i) {
        if (residuals[i] <= outlier_thresh) {
            inlier_points.push_back(points[i]);
        }
    }

    if (inlier_points.size() >= 3 && inlier_points.size() < points.size()) {
        double refit_cx = 0.0;
        double refit_cy = 0.0;
        double refit_r = 0.0;
        if (fitCircleTaubin(inlier_points, refit_cx, refit_cy, refit_r)) {
            cx = refit_cx;
            cy = refit_cy;
            radius = refit_r;
        }
    }

    // 4. 统计结果指标
    result.rotation_center = Point2D(cx, cy);
    result.rotation_radius = radius;
    result.valid_point_count = n_pts;
    result.detected_points.clear();
    result.point_radii = radii;
    result.point_angles_deg.clear();
    result.radial_residuals.clear();

    double sq_sum = 0.0;
    double max_err = 0.0;

    for (int i = 0; i < n_pts; ++i) {
        result.detected_points.push_back(Point2D(points[i].x, points[i].y));
        const double d = std::hypot(points[i].x - cx, points[i].y - cy);
        const double err = d - radius;
        sq_sum += err * err;
        max_err = std::max(max_err, std::abs(err));

        double angle = std::atan2(points[i].y - cy, points[i].x - cx) * (180.0 / CV_PI);
        if (angle < 0.0) {
            angle += 360.0;
        }
        result.point_angles_deg.push_back(angle);
        result.radial_residuals.push_back(err);
    }

    result.rms_error = std::sqrt(sq_sum / n_pts);
    result.max_error = max_err;

    // 5. 渲染可视化大诊断图
    diagnostic_bgr = cv::Mat(height, width, CV_8UC3);
    cv::cvtColor(mono8_images[0], diagnostic_bgr, cv::COLOR_GRAY2BGR);

    // 绘制完整拟合大圆轨迹 (荧光绿粗线)
    const cv::Point center_pt(static_cast<int>(std::round(cx)), static_cast<int>(std::round(cy)));
    const int r_int = static_cast<int>(std::round(radius));
    cv::circle(diagnostic_bgr, center_pt, r_int, cv::Scalar(0, 255, 0), 4, cv::LINE_AA);

    // 绘制旋转中心十字与靶心同心圆
    cv::circle(diagnostic_bgr, center_pt, 10, cv::Scalar(0, 0, 255), -1, cv::LINE_AA);
    cv::circle(diagnostic_bgr, center_pt, 30, cv::Scalar(0, 0, 255), 3, cv::LINE_AA);
    cv::line(diagnostic_bgr, cv::Point(center_pt.x - 70, center_pt.y), cv::Point(center_pt.x + 70, center_pt.y), cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
    cv::line(diagnostic_bgr, cv::Point(center_pt.x, center_pt.y - 70), cv::Point(center_pt.x, center_pt.y + 70), cv::Scalar(0, 0, 255), 2, cv::LINE_AA);

    std::ostringstream ss_cor;
    ss_cor << std::fixed << std::setprecision(2) << "COR (" << cx << ", " << cy << ")";
    cv::putText(diagnostic_bgr, ss_cor.str(), cv::Point(center_pt.x + 40, center_pt.y - 20),
                cv::FONT_HERSHEY_SIMPLEX, 1.1, cv::Scalar(0, 255, 255), 3, cv::LINE_AA);

    // 绘制各点位与放大 30 倍残差向量
    for (int i = 0; i < n_pts; ++i) {
        const cv::Point pt(static_cast<int>(std::round(points[i].x)), static_cast<int>(std::round(points[i].y)));
        // 射线连接旋转中心
        cv::line(diagnostic_bgr, center_pt, pt, cv::Scalar(180, 180, 180), 1, cv::LINE_AA);

        // 点标记
        cv::circle(diagnostic_bgr, pt, 8, cv::Scalar(0, 255, 255), -1, cv::LINE_AA);
        cv::circle(diagnostic_bgr, pt, static_cast<int>(std::round(radii[i])), cv::Scalar(0, 255, 255), 2, cv::LINE_AA);

        // 残差放大标尺
        const double d = std::hypot(points[i].x - cx, points[i].y - cy);
        const double err = result.radial_residuals[i];
        if (d > 1e-4) {
            const cv::Point2d u_dir = (points[i] - cv::Point2d(cx, cy)) * (1.0 / d);
            const cv::Point2d proj_on_circle = cv::Point2d(cx, cy) + u_dir * radius;
            const cv::Point2d err_end = proj_on_circle + u_dir * (err * 30.0);

            cv::line(diagnostic_bgr,
                     cv::Point(static_cast<int>(std::round(proj_on_circle.x)), static_cast<int>(std::round(proj_on_circle.y))),
                     cv::Point(static_cast<int>(std::round(err_end.x)), static_cast<int>(std::round(err_end.y))),
                     cv::Scalar(255, 0, 255), 3, cv::LINE_AA);

            // 标注文字
            std::ostringstream ss_lbl;
            const int nominal_angle = i * (360 / n_pts);
            ss_lbl << "P" << i << " (" << nominal_angle << "deg): err=" << std::showpos << std::fixed << std::setprecision(2) << err << "px";
            const cv::Point2d text_pos = points[i] + u_dir * 55.0;
            cv::putText(diagnostic_bgr, ss_lbl.str(),
                        cv::Point(static_cast<int>(std::round(text_pos.x)) - 80, static_cast<int>(std::round(text_pos.y))),
                        cv::FONT_HERSHEY_SIMPLEX, 0.85, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
        }
    }

    // 绘制左上角工业级 HUD 数据看板
    const int hud_x = 50;
    const int hud_y = 50;
    const int hud_w = 780;
    const int hud_h = 360;
    cv::Rect hud_rect(hud_x, hud_y, hud_w, hud_h);
    cv::Mat hud_roi = diagnostic_bgr(hud_rect);
    hud_roi = hud_roi * 0.35 + cv::Scalar(20, 25, 30) * 0.65;
    cv::rectangle(diagnostic_bgr, hud_rect, cv::Scalar(0, 180, 255), 2, cv::LINE_AA);

    std::vector<std::string> hud_lines;
    hud_lines.push_back("=== Platform Rotation Center Calibration ===");
    hud_lines.push_back(cv::format("Points: %d / %d  (Step: %.1f deg)", n_pts, n_pts, 360.0 / n_pts));
    hud_lines.push_back(cv::format("Center (Cx, Cy): (%.2f, %.2f) px", cx, cy));
    hud_lines.push_back(cv::format("Radius (R): %.2f px", radius));
    hud_lines.push_back(cv::format("Fitting RMS Error: %.3f px", result.rms_error));
    hud_lines.push_back(cv::format("Max Radial Deviation: %.3f px", result.max_error));
    hud_lines.push_back("Status: SUCCESS / HIGH ACCURACY");

    int line_y = hud_y + 42;
    for (size_t l = 0; l < hud_lines.size(); ++l) {
        cv::Scalar color = (l == 0) ? cv::Scalar(0, 255, 255) : (l == hud_lines.size() - 1 ? cv::Scalar(0, 255, 0) : cv::Scalar(240, 240, 240));
        double scale = (l == 0) ? 0.95 : 0.85;
        cv::putText(diagnostic_bgr, hud_lines[l], cv::Point(hud_x + 24, line_y), cv::FONT_HERSHEY_SIMPLEX, scale, color, 2, cv::LINE_AA);
        line_y += 46;
    }

    // 右上角画中画放大特写
    if (!first_roi_vis.empty()) {
        const int inset_size = 320;
        const int inset_x = width - inset_size - 60;
        const int inset_y = 60;
        cv::Rect inset_rect(inset_x, inset_y, inset_size, inset_size);
        cv::Mat resized_roi;
        cv::resize(first_roi_vis, resized_roi, cv::Size(inset_size, inset_size), 0, 0, cv::INTER_NEAREST);
        resized_roi.copyTo(diagnostic_bgr(inset_rect));
        cv::rectangle(diagnostic_bgr, inset_rect, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
        cv::putText(diagnostic_bgr, "Micro Marker (P0)", cv::Point(inset_x + 10, inset_y + 26),
                    cv::FONT_HERSHEY_SIMPLEX, 0.75, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
    }

    return Status::OK();
}

} // namespace wafer_calib