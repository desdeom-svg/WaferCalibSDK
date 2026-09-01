#include "wafer_calib/modules/circle_center_offset.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace wafer_calib {
namespace {

struct CircleCandidate {
    double cx = 0.0;
    double cy = 0.0;
    double r = 0.0;
    double circularity = 0.0;
    double inlier_ratio = 1.0;
    double rms = 0.0;
    double edge_energy = 0.0;
    double score = 0.0;
    std::vector<cv::Point> contour;
    bool is_bright = true;
};

// 基础最小二乘代数圆拟合 (Kåsa 方法: x^2 + y^2 + a*x + b*y + c = 0)
bool fitCircleLeastSquares(
    const std::vector<cv::Point>& points,
    double& out_cx,
    double& out_cy,
    double& out_r,
    double& out_rms
) {
    const size_t point_count = points.size();
    if (point_count < 5) {
        return false;
    }

    cv::Mat mat_a(static_cast<int>(point_count), 3, CV_64F);
    cv::Mat mat_b(static_cast<int>(point_count), 1, CV_64F);

    for (size_t i = 0; i < point_count; ++i) {
        const double x = static_cast<double>(points[i].x);
        const double y = static_cast<double>(points[i].y);
        mat_a.at<double>(static_cast<int>(i), 0) = x;
        mat_a.at<double>(static_cast<int>(i), 1) = y;
        mat_a.at<double>(static_cast<int>(i), 2) = 1.0;
        mat_b.at<double>(static_cast<int>(i), 0) = -(x * x + y * y);
    }

    cv::Mat solution;
    const bool solved = cv::solve(mat_a, mat_b, solution, cv::DECOMP_SVD);
    if (!solved) {
        return false;
    }

    const double a = solution.at<double>(0, 0);
    const double b = solution.at<double>(1, 0);
    const double c = solution.at<double>(2, 0);

    const double center_x = -a / 2.0;
    const double center_y = -b / 2.0;
    const double r_squared = (a * a + b * b) / 4.0 - c;

    if (r_squared <= 0.0) {
        return false;
    }

    const double radius = std::sqrt(r_squared);
    out_cx = center_x;
    out_cy = center_y;
    out_r = radius;

    double sum_sq_err = 0.0;
    for (size_t i = 0; i < point_count; ++i) {
        const double dx = points[i].x - center_x;
        const double dy = points[i].y - center_y;
        const double dist = std::sqrt(dx * dx + dy * dy);
        const double err = dist - radius;
        sum_sq_err += err * err;
    }

    out_rms = std::sqrt(sum_sq_err / static_cast<double>(point_count));
    return true;
}

// RANSAC 稳健圆拟合 (抗脏污粘连与局部突变离群点)
bool fitCircleRansac(
    const std::vector<cv::Point>& contour,
    double& out_cx,
    double& out_cy,
    double& out_r,
    double& out_rms,
    double& out_inlier_ratio,
    int max_iterations = 60,
    double inlier_thresh = 1.5
) {
    const size_t n = contour.size();
    if (n < 5) {
        return false;
    }

    // 1. 先进行一次全点集初拟合
    double init_cx = 0.0;
    double init_cy = 0.0;
    double init_r = 0.0;
    double init_rms = 0.0;
    std::vector<int> best_inliers;

    if (fitCircleLeastSquares(contour, init_cx, init_cy, init_r, init_rms)) {
        for (size_t i = 0; i < n; ++i) {
            const double dx = contour[i].x - init_cx;
            const double dy = contour[i].y - init_cy;
            const double dist = std::sqrt(dx * dx + dy * dy);
            if (std::abs(dist - init_r) < inlier_thresh) {
                best_inliers.push_back(static_cast<int>(i));
            }
        }
        // 若初拟合内点率极高，直接收敛
        if (best_inliers.size() >= static_cast<size_t>(0.85 * n)) {
            out_cx = init_cx;
            out_cy = init_cy;
            out_r = init_r;
            out_rms = init_rms;
            out_inlier_ratio = static_cast<double>(best_inliers.size()) / static_cast<double>(n);
            return true;
        }
    }

    // 2. RANSAC 确定性伪随机采样三点建立假设模型
    uint32_t rng_state = 123456789U;
    auto fast_rand = [&rng_state]() -> uint32_t {
        rng_state ^= (rng_state << 13);
        rng_state ^= (rng_state >> 17);
        rng_state ^= (rng_state << 5);
        return rng_state;
    };

    for (int iter = 0; iter < max_iterations; ++iter) {
        const size_t idx1 = fast_rand() % n;
        size_t idx2 = fast_rand() % n;
        while (idx2 == idx1) idx2 = fast_rand() % n;
        size_t idx3 = fast_rand() % n;
        while (idx3 == idx1 || idx3 == idx2) idx3 = fast_rand() % n;

        const cv::Point2d p1(contour[idx1].x, contour[idx1].y);
        const cv::Point2d p2(contour[idx2].x, contour[idx2].y);
        const cv::Point2d p3(contour[idx3].x, contour[idx3].y);

        const double d = 2.0 * (p1.x * (p2.y - p3.y) + p2.x * (p3.y - p1.y) + p3.x * (p1.y - p2.y));
        if (std::abs(d) < 1e-6) {
            continue;
        }

        const double p1_sq = p1.x * p1.x + p1.y * p1.y;
        const double p2_sq = p2.x * p2.x + p2.y * p2.y;
        const double p3_sq = p3.x * p3.x + p3.y * p3.y;

        const double sample_cx = (p1_sq * (p2.y - p3.y) + p2_sq * (p3.y - p1.y) + p3_sq * (p1.y - p2.y)) / d;
        const double sample_cy = (p1_sq * (p3.x - p2.x) + p2_sq * (p1.x - p3.x) + p3_sq * (p2.x - p1.x)) / d;
        const double sample_r = std::sqrt((p1.x - sample_cx) * (p1.x - sample_cx) + (p1.y - sample_cy) * (p1.y - sample_cy));

        if (sample_r < 8.0 || sample_r > 2000.0) {
            continue;
        }

        std::vector<int> current_inliers;
        current_inliers.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const double dx = contour[i].x - sample_cx;
            const double dy = contour[i].y - sample_cy;
            const double dist = std::sqrt(dx * dx + dy * dy);
            if (std::abs(dist - sample_r) < inlier_thresh) {
                current_inliers.push_back(static_cast<int>(i));
            }
        }

        if (current_inliers.size() > best_inliers.size()) {
            best_inliers = std::move(current_inliers);
        }
    }

    if (best_inliers.size() < 5 || best_inliers.size() < static_cast<size_t>(0.35 * n)) {
        return false;
    }

    // 3. 提取所有内点进行最小二乘精拟合
    std::vector<cv::Point> inlier_pts;
    inlier_pts.reserve(best_inliers.size());
    for (int idx : best_inliers) {
        inlier_pts.push_back(contour[idx]);
    }

    if (!fitCircleLeastSquares(inlier_pts, out_cx, out_cy, out_r, out_rms)) {
        return false;
    }

    out_inlier_ratio = static_cast<double>(best_inliers.size()) / static_cast<double>(n);
    return true;
}

// 绘制检测与中心偏移诊断图
void drawCircleDiagnostic(
    const cv::Mat& mono8,
    const CircleDetectionResult& result,
    const std::vector<cv::Point>& contour,
    cv::Mat& result_bgr
) {
    if (result_bgr.empty() || result_bgr.rows != mono8.rows || result_bgr.cols != mono8.cols || result_bgr.type() != CV_8UC3) {
        cv::cvtColor(mono8, result_bgr, cv::COLOR_GRAY2BGR);
    } else {
        cv::cvtColor(mono8, result_bgr, cv::COLOR_GRAY2BGR);
    }

    const int img_w = mono8.cols;
    const int img_h = mono8.rows;

    // 1. 绘制图像中心 (红色十字及文字)
    const cv::Point img_center_pt(cvRound(result.image_center.x), cvRound(result.image_center.y));
    cv::drawMarker(result_bgr, img_center_pt, cv::Scalar(0, 0, 255), cv::MARKER_CROSS, 40, 2, cv::LINE_AA);

    // 动态确定图像中心文字偏移量，避免与圆中心文字交叉
    cv::Point img_text_offset(-160, 28);
    if (result.offset_x < 0) {
        img_text_offset.x = 15;
    }
    if (result.offset_y < 0) {
        img_text_offset.y = 28;
    } else {
        img_text_offset.y = -15;
    }

    cv::putText(
        result_bgr,
        cv::format("ImgCenter(%.1f, %.1f)", result.image_center.x, result.image_center.y),
        img_center_pt + img_text_offset,
        cv::FONT_HERSHEY_SIMPLEX,
        0.55,
        cv::Scalar(0, 0, 255),
        1,
        cv::LINE_AA
    );

    // 2. 绘制检测到的圆真实轮廓 (黄色多边形)
    if (!contour.empty()) {
        std::vector<std::vector<cv::Point>> contours_to_draw = {contour};
        cv::drawContours(result_bgr, contours_to_draw, -1, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
    }

    // 3. 绘制拟合圆周 (青色圆环)
    const cv::Point circle_center_pt(cvRound(result.circle_center.x), cvRound(result.circle_center.y));
    const int draw_radius = std::max(1, cvRound(result.radius));
    cv::circle(result_bgr, circle_center_pt, draw_radius, cv::Scalar(255, 255, 0), 2, cv::LINE_AA);

    // 4. 绘制圆中心 (绿色十字及文字)
    cv::drawMarker(result_bgr, circle_center_pt, cv::Scalar(0, 255, 0), cv::MARKER_CROSS, 40, 2, cv::LINE_AA);

    cv::Point circle_text_offset(15, -15);
    if (result.offset_y >= 0) {
        circle_text_offset.y = 28;
    }

    cv::putText(
        result_bgr,
        cv::format("CircleCenter(%.2f, %.2f)", result.circle_center.x, result.circle_center.y),
        circle_center_pt + circle_text_offset,
        cv::FONT_HERSHEY_SIMPLEX,
        0.55,
        cv::Scalar(0, 255, 0),
        1,
        cv::LINE_AA
    );

    // 5. 绘制两中心间的连线 (洋红色线段)
    cv::line(result_bgr, img_center_pt, circle_center_pt, cv::Scalar(255, 0, 255), 2, cv::LINE_AA);

    // 6. 左上角信息展示面板
    const std::string text_offset = cv::format("Offset: dx=%.3f px, dy=%.3f px", result.offset_x, result.offset_y);
    const std::string text_circle = cv::format("Circle: R=%.3f px, Circ=%.3f, RMS=%.3f", result.radius, result.circularity, result.rms_residual);

    // 半透明背景框以保证文字清晰可见
    const int margin_x = 20;
    const int margin_y = 20;
    const cv::Rect bg_rect(margin_x - 10, margin_y - 10, 520, 80);
    if (bg_rect.x + bg_rect.width <= img_w && bg_rect.y + bg_rect.height <= img_h) {
        cv::Mat roi = result_bgr(bg_rect);
        cv::Mat black_box(roi.size(), CV_8UC3, cv::Scalar(0, 0, 0));
        cv::addWeighted(roi, 0.35, black_box, 0.65, 0, roi);
    }

    cv::putText(result_bgr, text_offset, cv::Point(margin_x, margin_y + 25), cv::FONT_HERSHEY_SIMPLEX, 0.75, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
    cv::putText(result_bgr, text_circle, cv::Point(margin_x, margin_y + 55), cv::FONT_HERSHEY_SIMPLEX, 0.75, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
}

} // namespace

Status CircleCenterOffsetModule::findCircleCenterOffset(
    const cv::Mat& mono8,
    CircleDetectionResult& result,
    cv::Mat& result_bgr
) {
    if (mono8.empty()) {
        return Status::Error(ErrorCode::ImageEmpty, "输入图像为空");
    }
    if (mono8.type() != CV_8UC1) {
        return Status::Error(ErrorCode::ImageFormatMismatch, "仅支持 CV_8UC1 (Mono8) 灰度图像");
    }
    if (mono8.cols < 16 || mono8.rows < 16) {
        return Status::Error(ErrorCode::InvalidParam, "图像尺寸过小，无法进行特征提取");
    }

    const int img_w = mono8.cols;
    const int img_h = mono8.rows;
    const double total_pixels = static_cast<double>(img_w * img_h);
    const Point2D image_center(img_w / 2.0, img_h / 2.0);

    // 1. 多尺度高斯差分 (DoG) 去除全局不均匀背景并增强边缘
    cv::Mat g_small;
    cv::Mat g_large;
    cv::GaussianBlur(mono8, g_small, cv::Size(9, 9), 2.0);
    cv::GaussianBlur(mono8, g_large, cv::Size(65, 65), 15.0);

    cv::Mat diff_pos;
    cv::Mat diff_neg;
    cv::subtract(g_small, g_large, diff_pos);
    cv::subtract(g_large, g_small, diff_neg);

    // 计算梯度幅值图用于边缘能量加权
    cv::Mat gx;
    cv::Mat gy;
    cv::Sobel(g_small, gx, CV_32F, 1, 0, 3);
    cv::Sobel(g_small, gy, CV_32F, 0, 1, 3);
    cv::Mat grad_mag;
    cv::magnitude(gx, gy, grad_mag);

    std::vector<CircleCandidate> candidates;

    // 分别尝试亮圆特征与暗圆特征
    const std::pair<cv::Mat, bool> passes[2] = {
        {diff_pos, true},
        {diff_neg, false}
    };

    const cv::Mat morph_kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));

    for (const auto& pass_item : passes) {
        const cv::Mat& diff_img = pass_item.first;
        const bool is_bright = pass_item.second;

        double min_val = 0.0;
        double max_val = 0.0;
        cv::minMaxLoc(diff_img, &min_val, &max_val);

        if (max_val < 3.0) {
            continue; // 信号微弱，跳过
        }

        // 计算差分图像的高响应分位数直方图
        const int hist_size = 256;
        float range[] = {0, 256};
        const float* hist_range = {range};
        cv::Mat hist;
        cv::calcHist(&diff_img, 1, 0, cv::Mat(), hist, 1, &hist_size, &hist_range, true, false);

        const double target_count_98 = total_pixels * 0.98;
        const double target_count_995 = total_pixels * 0.995;

        double accum = 0.0;
        int th_98 = 0;
        int th_995 = 0;

        for (int i = 0; i < 256; ++i) {
            accum += hist.at<float>(i);
            if (accum >= target_count_98 && th_98 == 0) {
                th_98 = i;
            }
            if (accum >= target_count_995 && th_995 == 0) {
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

        // 形态学滤波消除噪点与连接断线
        cv::Mat binary_clean;
        cv::morphologyEx(binary, binary_clean, cv::MORPH_CLOSE, morph_kernel);
        cv::morphologyEx(binary_clean, binary_clean, cv::MORPH_OPEN, morph_kernel);

        // 提取外部轮廓
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary_clean, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

        for (const auto& c : contours) {
            const double area = cv::contourArea(c);
            // 过滤微小脏污与过大背景 (目标圆面积通常在 250 像素以上)
            if (area < 250.0 || area > 0.6 * total_pixels) {
                continue;
            }

            const double perimeter = cv::arcLength(c, true);
            if (perimeter <= 0.0) {
                continue;
            }

            const double circularity = 4.0 * CV_PI * area / (perimeter * perimeter);
            if (circularity < 0.45) {
                continue; // 圆度过低
            }

            // 几何长宽比检查
            const cv::RotatedRect min_rect = cv::minAreaRect(c);
            const float rect_w = min_rect.size.width;
            const float rect_h = min_rect.size.height;
            if (rect_w > 0.0f && rect_h > 0.0f) {
                const double aspect_ratio = static_cast<double>(std::max(rect_w, rect_h)) /
                                            static_cast<double>(std::min(rect_w, rect_h));
                if (aspect_ratio > 1.55) {
                    continue; // 明显非圆
                }
            }

            double fit_cx = 0.0;
            double fit_cy = 0.0;
            double fit_r = 0.0;
            double fit_rms = 0.0;
            double inlier_ratio = 1.0;

            // 采用 RANSAC 稳健圆拟合
            if (!fitCircleRansac(c, fit_cx, fit_cy, fit_r, fit_rms, inlier_ratio)) {
                continue;
            }

            if (fit_r < 10.0 || fit_r > std::min(img_w, img_h) / 2.0) {
                continue;
            }

            // 计算圆周边缘平均梯度能量 (采样 36 个方向)
            double edge_energy_sum = 0.0;
            const int sample_num = 36;
            for (int k = 0; k < sample_num; ++k) {
                const double angle = k * (2.0 * CV_PI / sample_num);
                const int sx = std::clamp(cvRound(fit_cx + fit_r * std::cos(angle)), 0, img_w - 1);
                const int sy = std::clamp(cvRound(fit_cy + fit_r * std::sin(angle)), 0, img_h - 1);
                edge_energy_sum += grad_mag.at<float>(sy, sx);
            }
            const double edge_energy = edge_energy_sum / sample_num;

            // 视场中心距离惩罚因子
            const double dist_to_center = std::sqrt((fit_cx - image_center.x) * (fit_cx - image_center.x) +
                                                    (fit_cy - image_center.y) * (fit_cy - image_center.y));
            const double center_weight = 1.0 / (1.0 + dist_to_center / (img_w * 0.5));
            const double area_weight = std::log10(area);

            // 综合打分: 圆度 + RANSAC内点率 - RMS残差 + 边缘能量
            const double score = (circularity * 5.0 + inlier_ratio * 5.0 - fit_rms * 0.5 + edge_energy * 0.2) *
                                 area_weight * center_weight;

            CircleCandidate cand;
            cand.cx = fit_cx;
            cand.cy = fit_cy;
            cand.r = fit_r;
            cand.circularity = circularity;
            cand.inlier_ratio = inlier_ratio;
            cand.rms = fit_rms;
            cand.edge_energy = edge_energy;
            cand.score = score;
            cand.contour = c;
            cand.is_bright = is_bright;

            candidates.push_back(cand);
        }
    }

    if (candidates.empty()) {
        return Status::Error(ErrorCode::FeatureNotFound, "未能在图像中检测到符合条件的圆形特征");
    }

    // 按综合得分从高到低排序，选择最优圆候选
    std::sort(candidates.begin(), candidates.end(), [](const CircleCandidate& a, const CircleCandidate& b) {
        return a.score > b.score;
    });

    const CircleCandidate& best = candidates.front();

    result.circle_center = Point2D(best.cx, best.cy);
    result.radius = best.r;
    result.image_center = image_center;
    result.offset_x = best.cx - image_center.x;
    result.offset_y = best.cy - image_center.y;
    result.circularity = best.circularity;
    result.rms_residual = best.rms;

    drawCircleDiagnostic(mono8, result, best.contour, result_bgr);

    return Status::OK();
}

} // namespace wafer_calib
