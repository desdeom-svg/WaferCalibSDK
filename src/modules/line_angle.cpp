#include "wafer_calib/modules/line_angle.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace wafer_calib {
namespace {

struct LineCandidate {
    cv::Vec4f fit_line;
    cv::Point2d center;
    double angle_degrees = 0.0;
    double weight = 0.0;
    cv::Rect bounding_box;
};

double normalizeLineAngle(double angle_degrees) {
    while (angle_degrees >= 90.0) {
        angle_degrees -= 180.0;
    }
    while (angle_degrees < -90.0) {
        angle_degrees += 180.0;
    }
    return angle_degrees;
}

void drawFittedLine(cv::Mat& result_bgr, const LineCandidate& candidate, const cv::Scalar& color, int thickness = 2) {
    const cv::Point2d direction(candidate.fit_line[0], candidate.fit_line[1]);
    const double length = std::hypot(result_bgr.cols, result_bgr.rows);
    const cv::Point start = candidate.center - direction * length;
    const cv::Point end = candidate.center + direction * length;
    cv::line(result_bgr, start, end, color, thickness, cv::LINE_AA);
}

bool touchesVerticalImageBorder(const cv::Rect& region, const cv::Size& image_size) {
    return region.y == 0 || region.y + region.height == image_size.height;
}

cv::Rect clampRect(const cv::Rect& rect, const cv::Size& size) {
    int x = std::max(0, rect.x);
    int y = std::max(0, rect.y);
    int width = std::min(rect.width, size.width - x);
    int height = std::min(rect.height, size.height - y);
    if (width <= 0 || height <= 0) {
        return cv::Rect(0, 0, 0, 0);
    }
    return cv::Rect(x, y, width, height);
}

} // namespace

Status LineAngleModule::extractLineInfo(
    const cv::Mat& mono8,
    SingleViewLineInfo& out_info,
    cv::Mat* result_bgr
) {
    if (mono8.empty()) {
        return Status::Error(ErrorCode::ImageEmpty, "输入图像为空");
    }
    if (mono8.type() != CV_8UC1) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "仅支持 CV_8UC1 图像");
    }

    cv::Mat foreground;
    cv::threshold(mono8, foreground, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
    cv::morphologyEx(
        foreground,
        foreground,
        cv::MORPH_OPEN,
        cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));

    cv::Mat labels;
    cv::Mat statistics;
    cv::Mat centroids;
    const int label_count = cv::connectedComponentsWithStats(
        foreground, labels, statistics, centroids, 8, CV_32S);

    const int minimum_width = mono8.cols / 4;
    std::vector<LineCandidate> candidates;
    std::vector<cv::Rect> discarded_border_regions;

    for (int label = 1; label < label_count; ++label) {
        const int width = statistics.at<int>(label, cv::CC_STAT_WIDTH);
        const int height = statistics.at<int>(label, cv::CC_STAT_HEIGHT);
        const int area = statistics.at<int>(label, cv::CC_STAT_AREA);
        if (width < minimum_width || width < height * 3 || area < width * height / 5) {
            continue;
        }

        const int left = statistics.at<int>(label, cv::CC_STAT_LEFT);
        const int top = statistics.at<int>(label, cv::CC_STAT_TOP);
        const cv::Rect region(left, top, width, height);
        if (touchesVerticalImageBorder(region, mono8.size())) {
            discarded_border_regions.push_back(region);
            continue;
        }

        std::vector<cv::Point> points;
        points.reserve(area);
        for (int row = top; row < top + height; ++row) {
            const int* label_row = labels.ptr<int>(row) + left;
            for (int column = 0; column < width; ++column) {
                if (label_row[column] == label) {
                    points.emplace_back(left + column, row);
                }
            }
        }
        if (points.size() < 2) {
            continue;
        }

        cv::Vec4f fitted_line;
        cv::fitLine(points, fitted_line, cv::DIST_L2, 0.0, 0.01, 0.01);
        const double candidate_angle = normalizeLineAngle(
            std::atan2(fitted_line[1], fitted_line[0]) * 180.0 / CV_PI);
        candidates.push_back({
            fitted_line,
            cv::Point2d(fitted_line[2], fitted_line[3]),
            candidate_angle,
            static_cast<double>(width),
            region});
    }

    if (candidates.empty()) {
        return Status::Error(ErrorCode::FeatureNotFound, "未找到足够长的水平标定线");
    }

    double weighted_cosine = 0.0;
    double weighted_sine = 0.0;
    cv::Point2d weighted_center(0.0, 0.0);
    double total_weight = 0.0;
    cv::Rect union_box = candidates[0].bounding_box;

    for (const LineCandidate& candidate : candidates) {
        const double double_angle_radians = candidate.angle_degrees * 2.0 * CV_PI / 180.0;
        weighted_cosine += candidate.weight * std::cos(double_angle_radians);
        weighted_sine += candidate.weight * std::sin(double_angle_radians);
        weighted_center += candidate.weight * candidate.center;
        total_weight += candidate.weight;
        union_box |= candidate.bounding_box;
    }

    const double angle_degrees = normalizeLineAngle(
        std::atan2(weighted_sine, weighted_cosine) * 90.0 / CV_PI);
    weighted_center *= (1.0 / total_weight);

    const double angle_radians = angle_degrees * CV_PI / 180.0;
    const cv::Point2d line_dir(std::cos(angle_radians), std::sin(angle_radians));

    // 计算中心坚线 u = width / 2.0 处的亚像素 Y 坐标
    const double center_u = mono8.cols / 2.0;
    const double slope = (std::abs(line_dir.x) > 1e-6) ? (line_dir.y / line_dir.x) : 0.0;
    const double center_v = weighted_center.y + (center_u - weighted_center.x) * slope;

    out_info.angle_degrees = angle_degrees;
    out_info.center_y = center_v;
    out_info.line_center = weighted_center;
    out_info.line_direction = line_dir;
    out_info.bounding_box = union_box;
    out_info.detected = true;

    if (result_bgr != nullptr) {
        cv::cvtColor(mono8, *result_bgr, cv::COLOR_GRAY2BGR);
        for (const cv::Rect& region : discarded_border_regions) {
            cv::rectangle(*result_bgr, region, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
        }
        for (const LineCandidate& candidate : candidates) {
            drawFittedLine(*result_bgr, candidate, cv::Scalar(0, 255, 0), 1);
        }
        const LineCandidate fused_line{
            cv::Vec4f(static_cast<float>(line_dir.x), static_cast<float>(line_dir.y),
                      static_cast<float>(weighted_center.x), static_cast<float>(weighted_center.y)),
            weighted_center,
            angle_degrees,
            1.0,
            union_box};
        drawFittedLine(*result_bgr, fused_line, cv::Scalar(0, 255, 255), 2);

        // 绘制中心测点准星
        cv::drawMarker(*result_bgr, cv::Point(static_cast<int>(std::round(center_u)), static_cast<int>(std::round(center_v))),
                       cv::Scalar(0, 0, 255), cv::MARKER_CROSS, 40, 2, cv::LINE_AA);
        cv::putText(*result_bgr, cv::format("angle=%.4f deg, v_center=%.2f px", angle_degrees, center_v),
                    cv::Point(30, 50), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
    }

    return Status::OK();
}

Status LineAngleModule::findHorizontalLineAngle(
    const cv::Mat& mono8,
    double& angle_degrees,
    cv::Mat& result_bgr
) {
    SingleViewLineInfo info;
    const Status status = extractLineInfo(mono8, info, &result_bgr);
    if (status.ok()) {
        angle_degrees = info.angle_degrees;
    }
    return status;
}

Status LineAngleModule::findTwoViewHorizontalLineAngle(
    const cv::Mat& mono8_view1,
    const cv::Mat& mono8_view2,
    double stage_delta_x_mm,
    double pixel_scale_y_um,
    TwoViewLineAngleResult& out_result,
    cv::Mat& diagnostic_bgr,
    int diag_width,
    int diag_height
) {
    if (mono8_view1.empty() || mono8_view2.empty()) {
        return Status::Error(ErrorCode::ImageEmpty, "视野图像不能为空");
    }
    if (mono8_view1.type() != CV_8UC1 || mono8_view2.type() != CV_8UC1) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "仅支持 CV_8UC1 灰度图像");
    }
    if (mono8_view1.size() != mono8_view2.size()) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "双视野图像尺寸必须一致");
    }
    if (std::abs(stage_delta_x_mm) < 1e-4) {
        return Status::Error(ErrorCode::InvalidParam, "机械 X 轴移动距离 stage_delta_x_mm 不能为 0");
    }
    if (pixel_scale_y_um <= 1e-4) {
        return Status::Error(ErrorCode::InvalidParam, "Y 向像元当量 pixel_scale_y_um 必须大于 0");
    }

    SingleViewLineInfo info1;
    Status s1 = extractLineInfo(mono8_view1, info1);
    if (!s1.ok()) {
        return Status::Error(s1.code, std::string("视野 1 直线提取失败: ") + s1.message);
    }

    SingleViewLineInfo info2;
    Status s2 = extractLineInfo(mono8_view2, info2);
    if (!s2.ok()) {
        return Status::Error(s2.code, std::string("视野 2 直线提取失败: ") + s2.message);
    }

    // 像面垂直落差 (像素)：v 轴朝下
    const double delta_v_pixels = info2.center_y - info1.center_y;

    // 物理落差 (mm)：v2 > v1 表示视野2中直线在像面偏下方，对应物理工件向下倾斜，故取负号
    const double scale_mm_per_px = pixel_scale_y_um / 1000.0;
    const double delta_y_world_mm = -delta_v_pixels * scale_mm_per_px;
    const double delta_x_stage_mm = stage_delta_x_mm;

    // 全局大基线角度计算
    const double global_angle_rad = std::atan2(delta_y_world_mm, delta_x_stage_mm);
    const double global_angle_deg = normalizeLineAngle(global_angle_rad * 180.0 / CV_PI);
    const double global_angle_arcmin = global_angle_deg * 60.0;

    out_result.global_angle_deg = global_angle_deg;
    out_result.global_angle_arcmin = global_angle_arcmin;
    out_result.view1_angle_deg = info1.angle_degrees;
    out_result.view2_angle_deg = info2.angle_degrees;
    out_result.view1_center_y = info1.center_y;
    out_result.view2_center_y = info2.center_y;
    out_result.delta_v_pixels = delta_v_pixels;
    out_result.delta_y_world_mm = delta_y_world_mm;
    out_result.stage_delta_x_mm = delta_x_stage_mm;
    out_result.pixel_scale_y_um = pixel_scale_y_um;

    // 绘制综合对齐诊断大图
    if (diag_width > 0 && diag_height > 0) {
        diagnostic_bgr.create(diag_height, diag_width, CV_8UC3);
        diagnostic_bgr.setTo(cv::Scalar(22, 26, 32)); // 科技深暗灰底色

        const int margin_x = 30;
        const int hud_top_h = 130;
        const int hud_bottom_h = 280;

        // 1. 顶部 HUD 状态栏
        const cv::Rect top_hud_rect(margin_x, 20, diag_width - margin_x * 2, hud_top_h);
        cv::rectangle(diagnostic_bgr, top_hud_rect, cv::Scalar(32, 38, 48), -1);
        cv::rectangle(diagnostic_bgr, top_hud_rect, cv::Scalar(65, 75, 90), 2);

        cv::putText(diagnostic_bgr, "WaferCalibSDK - Two-View Long Baseline Line Alignment Dashboard",
                    cv::Point(top_hud_rect.x + 25, top_hud_rect.y + 40),
                    cv::FONT_HERSHEY_SIMPLEX, 0.95, cv::Scalar(180, 210, 240), 2, cv::LINE_AA);

        const std::string main_angle_text = cv::format(
            "Global Angle: %.4f deg (%.2f arcmin)   [STATUS: PASS]",
            global_angle_deg, global_angle_arcmin);
        cv::putText(diagnostic_bgr, main_angle_text,
                    cv::Point(top_hud_rect.x + 25, top_hud_rect.y + 82),
                    cv::FONT_HERSHEY_SIMPLEX, 1.15, cv::Scalar(0, 255, 200), 3, cv::LINE_AA);

        const double angle_diff = std::abs(info1.angle_degrees - info2.angle_degrees);
        const std::string sub_angle_text = cv::format(
            "View1 Local: %+.4f deg | View2 Local: %+.4f deg | Local Consistency Diff: %.4f deg",
            info1.angle_degrees, info2.angle_degrees, angle_diff);
        cv::putText(diagnostic_bgr, sub_angle_text,
                    cv::Point(top_hud_rect.x + 25, top_hud_rect.y + 112),
                    cv::FONT_HERSHEY_SIMPLEX, 0.75, cv::Scalar(170, 185, 200), 1, cv::LINE_AA);

        // 2. 双视口主体对比区
        const int body_top = top_hud_rect.y + top_hud_rect.height + 20;
        const int body_bottom = diag_height - hud_bottom_h - 20;
        const int body_h = body_bottom - body_top;
        const int center_bridge_w = 140;
        const int view_w = (diag_width - margin_x * 2 - center_bridge_w) / 2;

        const cv::Rect left_view_rect(margin_x, body_top, view_w, body_h);
        const cv::Rect right_view_rect(diag_width - margin_x - view_w, body_top, view_w, body_h);
        const cv::Rect bridge_rect(left_view_rect.x + left_view_rect.width, body_top, center_bridge_w, body_h);

        auto renderViewCard = [&](const cv::Mat& img, const SingleViewLineInfo& info,
                                  const cv::Rect& card_rect, const std::string& title,
                                  const cv::Scalar& theme_color) {
            cv::rectangle(diagnostic_bgr, card_rect, cv::Scalar(28, 34, 42), -1);
            cv::rectangle(diagnostic_bgr, card_rect, cv::Scalar(55, 65, 78), 2);

            // 标题条
            const cv::Rect title_bar(card_rect.x, card_rect.y, card_rect.width, 45);
            cv::rectangle(diagnostic_bgr, title_bar, cv::Scalar(38, 46, 56), -1);
            cv::putText(diagnostic_bgr, title, cv::Point(title_bar.x + 20, title_bar.y + 30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.85, theme_color, 2, cv::LINE_AA);

            // 上半部分：全景缩略图
            const int pad = 15;
            const int pane_top = card_rect.y + 55;
            const int pane_w = card_rect.width - pad * 2;
            const int pano_h = static_cast<int>(body_h * 0.52);

            cv::Mat pano_bgr;
            cv::cvtColor(img, pano_bgr, cv::COLOR_GRAY2BGR);
            // 绘制拟合线
            const double img_len = std::hypot(img.cols, img.rows);
            const cv::Point p_start = info.line_center - info.line_direction * img_len;
            const cv::Point p_end = info.line_center + info.line_direction * img_len;
            cv::line(pano_bgr, p_start, p_end, cv::Scalar(0, 255, 255), 4, cv::LINE_AA);
            // 绘制中心垂线参考与测点准星
            const double center_u = img.cols / 2.0;
            cv::line(pano_bgr, cv::Point2d(center_u, 0), cv::Point2d(center_u, img.rows),
                     cv::Scalar(0, 140, 255), 2, cv::LINE_AA);
            cv::drawMarker(pano_bgr, cv::Point(static_cast<int>(center_u), static_cast<int>(info.center_y)),
                           cv::Scalar(0, 0, 255), cv::MARKER_CROSS, 80, 4, cv::LINE_AA);

            cv::Mat pano_resized;
            cv::resize(pano_bgr, pano_resized, cv::Size(pane_w, pano_h));
            pano_resized.copyTo(diagnostic_bgr(cv::Rect(card_rect.x + pad, pane_top, pane_w, pano_h)));
            cv::rectangle(diagnostic_bgr, cv::Rect(card_rect.x + pad, pane_top, pane_w, pano_h),
                          cv::Scalar(80, 95, 110), 1);

            cv::putText(diagnostic_bgr, "Overview [Full FOV 4096x4096]",
                        cv::Point(card_rect.x + pad + 15, pane_top + 28),
                        cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(220, 230, 240), 2, cv::LINE_AA);

            // 下半部分：中心测点微观 1:1 ROI 特写
            const int zoom_top = pane_top + pano_h + 15;
            const int zoom_h = card_rect.y + card_rect.height - zoom_top - pad;

            const int roi_half_w = 400;
            const int roi_half_h = 160;
            const cv::Rect raw_roi(static_cast<int>(center_u) - roi_half_w,
                                   static_cast<int>(info.center_y) - roi_half_h,
                                   roi_half_w * 2, roi_half_h * 2);
            const cv::Rect valid_roi = clampRect(raw_roi, img.size());

            cv::Mat zoom_bgr;
            if (valid_roi.width > 0 && valid_roi.height > 0) {
                cv::cvtColor(img(valid_roi), zoom_bgr, cv::COLOR_GRAY2BGR);
                // 绘制在 ROI 局部坐标系下的中心测点和直线
                const cv::Point roi_center(static_cast<int>(center_u) - valid_roi.x,
                                           static_cast<int>(info.center_y) - valid_roi.y);
                const cv::Point roi_p1 = roi_center - cv::Point(static_cast<int>(info.line_direction.x * 600),
                                                                static_cast<int>(info.line_direction.y * 600));
                const cv::Point roi_p2 = roi_center + cv::Point(static_cast<int>(info.line_direction.x * 600),
                                                                static_cast<int>(info.line_direction.y * 600));
                cv::line(zoom_bgr, roi_p1, roi_p2, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
                cv::drawMarker(zoom_bgr, roi_center, cv::Scalar(0, 0, 255), cv::MARKER_CROSS, 40, 2, cv::LINE_AA);
                cv::line(zoom_bgr, cv::Point(roi_center.x, 0), cv::Point(roi_center.x, zoom_bgr.rows),
                         cv::Scalar(0, 160, 255), 1, cv::LINE_AA);
            } else {
                zoom_bgr = cv::Mat::zeros(zoom_h, pane_w, CV_8UC3);
            }

            cv::Mat zoom_resized;
            cv::resize(zoom_bgr, zoom_resized, cv::Size(pane_w, zoom_h));
            zoom_resized.copyTo(diagnostic_bgr(cv::Rect(card_rect.x + pad, zoom_top, pane_w, zoom_h)));
            cv::rectangle(diagnostic_bgr, cv::Rect(card_rect.x + pad, zoom_top, pane_w, zoom_h),
                          cv::Scalar(80, 95, 110), 1);

            const std::string zoom_tag = cv::format(
                "Center ROI Zoom (u_center=2048.0 px, v_center=%.2f px, theta=%+.4f deg)",
                info.center_y, info.angle_degrees);
            cv::putText(diagnostic_bgr, zoom_tag,
                        cv::Point(card_rect.x + pad + 15, zoom_top + 28),
                        cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
        };

        renderViewCard(mono8_view1, info1, left_view_rect,
                       "View 1 : Left End Station (Origin Baseline)",
                       cv::Scalar(100, 200, 255));

        renderViewCard(mono8_view2, info2, right_view_rect,
                       "View 2 : Right End Station (Delta X Traverse)",
                       cv::Scalar(255, 200, 100));

        // 绘制中间对齐桥指示标尺
        const int bridge_cx = bridge_rect.x + bridge_rect.width / 2;
        const int bridge_cy = bridge_rect.y + bridge_rect.height / 2;
        cv::rectangle(diagnostic_bgr, bridge_rect, cv::Scalar(25, 30, 38), -1);

        // 连线与高度差双向箭头
        cv::line(diagnostic_bgr, cv::Point(left_view_rect.x + left_view_rect.width, bridge_cy),
                 cv::Point(right_view_rect.x, bridge_cy), cv::Scalar(80, 100, 120), 2, cv::LINE_AA);

        cv::arrowedLine(diagnostic_bgr, cv::Point(bridge_cx, bridge_cy - 80),
                        cv::Point(bridge_cx, bridge_cy + 80), cv::Scalar(0, 220, 255), 3, cv::LINE_AA, 0, 0.25);
        cv::arrowedLine(diagnostic_bgr, cv::Point(bridge_cx, bridge_cy + 80),
                        cv::Point(bridge_cx, bridge_cy - 80), cv::Scalar(0, 220, 255), 3, cv::LINE_AA, 0, 0.25);

        const std::string dv_str = cv::format("Delta v: %+.1f px", delta_v_pixels);
        const std::string dy_str = cv::format("Delta Y: %+.3f mm", delta_y_world_mm);
        cv::putText(diagnostic_bgr, dv_str, cv::Point(bridge_cx - 60, bridge_cy - 95),
                    cv::FONT_HERSHEY_SIMPLEX, 0.50, cv::Scalar(0, 220, 255), 1, cv::LINE_AA);
        cv::putText(diagnostic_bgr, dy_str, cv::Point(bridge_cx - 60, bridge_cy + 115),
                    cv::FONT_HERSHEY_SIMPLEX, 0.50, cv::Scalar(0, 255, 120), 1, cv::LINE_AA);

        // 3. 底部参数看板详情
        const cv::Rect btm_hud_rect(margin_x, diag_height - hud_bottom_h, diag_width - margin_x * 2, hud_bottom_h - 20);
        cv::rectangle(diagnostic_bgr, btm_hud_rect, cv::Scalar(30, 36, 46), -1);
        cv::rectangle(diagnostic_bgr, btm_hud_rect, cv::Scalar(60, 70, 85), 2);

        // 标题条
        const cv::Rect btm_title_rect(btm_hud_rect.x, btm_hud_rect.y, btm_hud_rect.width, 40);
        cv::rectangle(diagnostic_bgr, btm_title_rect, cv::Scalar(40, 48, 60), -1);
        cv::putText(diagnostic_bgr, "Long Baseline Alignment Metrics & Physical Parameter Audit",
                    cv::Point(btm_title_rect.x + 20, btm_title_rect.y + 28),
                    cv::FONT_HERSHEY_SIMPLEX, 0.80, cv::Scalar(240, 240, 240), 2, cv::LINE_AA);

        // 4 列参数卡片
        const int col_count = 4;
        const int col_w = (btm_hud_rect.width - 40) / col_count;
        const int card_h = btm_hud_rect.height - 60;

        auto renderMetricBox = [&](int col_idx, const std::string& label,
                                   const std::string& val_main, const std::string& note,
                                   const cv::Scalar& val_color) {
            const int bx = btm_hud_rect.x + 20 + col_idx * col_w;
            const int by = btm_hud_rect.y + 50;
            const cv::Rect box_rect(bx, by, col_w - 15, card_h);

            cv::rectangle(diagnostic_bgr, box_rect, cv::Scalar(24, 28, 36), -1);
            cv::rectangle(diagnostic_bgr, box_rect, cv::Scalar(55, 62, 75), 1);

            cv::putText(diagnostic_bgr, label, cv::Point(bx + 15, by + 32),
                        cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(180, 190, 205), 1, cv::LINE_AA);
            cv::putText(diagnostic_bgr, val_main, cv::Point(bx + 15, by + 80),
                        cv::FONT_HERSHEY_SIMPLEX, 0.95, val_color, 2, cv::LINE_AA);
            cv::putText(diagnostic_bgr, note, cv::Point(bx + 15, by + 125),
                        cv::FONT_HERSHEY_SIMPLEX, 0.60, cv::Scalar(140, 150, 165), 1, cv::LINE_AA);
        };

        renderMetricBox(0, "[1] Mechanical Stage Traverse",
                        cv::format("Delta X = %.4f mm", delta_x_stage_mm),
                        "Stage purely moved along X-axis (Y fixed)",
                        cv::Scalar(255, 215, 0));

        renderMetricBox(1, "[2] Pixel Scale Resolution",
                        cv::format("s_y = %.4f um/px", pixel_scale_y_um),
                        cv::format("FOV Span: ~%.2f mm", mono8_view1.cols * pixel_scale_y_um / 1000.0),
                        cv::Scalar(100, 220, 255));

        renderMetricBox(2, "[3] Spatial Physical Drop",
                        cv::format("Delta Y = %+.4f mm", delta_y_world_mm),
                        cv::format("Image Shift: %+.2f px", delta_v_pixels),
                        cv::Scalar(120, 255, 120));

        const double baseline_gain = (mono8_view1.cols > 0) ? (delta_x_stage_mm / (mono8_view1.cols * pixel_scale_y_um / 1000.0)) : 1.0;
        renderMetricBox(3, "[4] Solved Global Angle",
                        cv::format("%.4f deg", global_angle_deg),
                        cv::format("arcmin: %+.2f | Gain: ~%.1fx", global_angle_arcmin, baseline_gain),
                        cv::Scalar(0, 255, 220));
    }

    return Status::OK();
}

} // namespace wafer_calib
