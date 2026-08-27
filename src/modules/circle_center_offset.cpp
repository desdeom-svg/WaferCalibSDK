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
    double rms = 0.0;
    double score = 0.0;
    std::vector<cv::Point> contour;
    bool is_bright = true;
};

// 代数圆拟合 (最小二乘法: x^2 + y^2 + a*x + b*y + c = 0)
bool fitCircleAlgebraic(
    const std::vector<cv::Point>& contour,
    double& out_cx,
    double& out_cy,
    double& out_r,
    double& out_rms
) {
    const size_t point_count = contour.size();
    if (point_count < 5) {
        return false;
    }

    // 构造超定方程组 A * [a, b, c]^T = B
    cv::Mat mat_a(static_cast<int>(point_count), 3, CV_64F);
    cv::Mat mat_b(static_cast<int>(point_count), 1, CV_64F);

    for (size_t i = 0; i < point_count; ++i) {
        const double x = static_cast<double>(contour[i].x);
        const double y = static_cast<double>(contour[i].y);
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

    // 计算几何残差 RMS
    double sum_sq_err = 0.0;
    for (size_t i = 0; i < point_count; ++i) {
        const double dx = contour[i].x - center_x;
        const double dy = contour[i].y - center_y;
        const double dist = std::sqrt(dx * dx + dy * dy);
        const double err = dist - radius;
        sum_sq_err += err * err;
    }

    out_rms = std::sqrt(sum_sq_err / static_cast<double>(point_count));
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

    // 1. 多尺度高斯差分 (DoG) 去除全局不均匀背景并增强边缘
    cv::Mat g_small;
    cv::Mat g_large;
    cv::GaussianBlur(mono8, g_small, cv::Size(9, 9), 2.0);
    cv::GaussianBlur(mono8, g_large, cv::Size(65, 65), 15.0);

    cv::Mat diff_pos;
    cv::Mat diff_neg;
    cv::subtract(g_small, g_large, diff_pos);
    cv::subtract(g_large, g_small, diff_neg);

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

        // 寻找 98.0% 与 99.5% 分位数阈值
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
            // 过滤面积过小或过大的轮廓
            if (area < 80.0 || area > 0.6 * total_pixels) {
                continue;
            }

            const double perimeter = cv::arcLength(c, true);
            if (perimeter <= 0.0) {
                continue;
            }

            const double circularity = 4.0 * CV_PI * area / (perimeter * perimeter);
            if (circularity < 0.50) {
                continue; // 圆度过低，非圆
            }

            // 几何长宽比检查
            const cv::RotatedRect min_rect = cv::minAreaRect(c);
            const float rect_w = min_rect.size.width;
            const float rect_h = min_rect.size.height;
            if (rect_w > 0.0f && rect_h > 0.0f) {
                const double aspect_ratio = static_cast<double>(std::max(rect_w, rect_h)) /
                                            static_cast<double>(std::min(rect_w, rect_h));
                if (aspect_ratio > 1.45) {
                    continue; // 明显非圆
                }
            }

            double fit_cx = 0.0;
            double fit_cy = 0.0;
            double fit_r = 0.0;
            double fit_rms = 0.0;

            if (!fitCircleAlgebraic(c, fit_cx, fit_cy, fit_r, fit_rms)) {
                continue;
            }

            if (fit_r < 3.0 || fit_r > std::min(img_w, img_h) / 2.0) {
                continue;
            }

            // 综合评价打分 (圆度高、残差小优先)
            const double score = circularity * 10.0 - fit_rms * 0.5;

            CircleCandidate cand;
            cand.cx = fit_cx;
            cand.cy = fit_cy;
            cand.r = fit_r;
            cand.circularity = circularity;
            cand.rms = fit_rms;
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

    // 图像几何中心 (width / 2.0, height / 2.0)
    const Point2D image_center(img_w / 2.0, img_h / 2.0);

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
