#include "wafer_calib/c_api/wafer_calib_c.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace {
using Points = std::vector<cv::Point2d>;

// 计算数组指定分位数（输入按值拷贝以支持原地 nth_element）
double quantile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    const size_t k = static_cast<size_t>(fraction * static_cast<double>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(k), values.end());
    return values[k];
}

// 计算一行内指定区间 [start_idx, end_idx) 的分位数（兼容 NumPy 线性插值百分位）
double slicePercentile(const std::vector<double>& row, int start_idx, int end_idx, double fraction) {
    const int n_total = static_cast<int>(row.size());
    const int s = std::max(0, std::min(n_total, start_idx));
    const int e = std::max(s, std::min(n_total, end_idx));
    if (e <= s) {
        return 0.0;
    }
    std::vector<double> sub(row.begin() + s, row.begin() + e);
    std::sort(sub.begin(), sub.end());
    if (sub.size() == 1) {
        return sub[0];
    }
    const double pos = fraction * static_cast<double>(sub.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const size_t hi = static_cast<size_t>(std::ceil(pos));
    const double weight = pos - static_cast<double>(lo);
    return sub[lo] * (1.0 - weight) + sub[hi] * weight;
}

// 局部二次曲线鲁棒拟合与物理切线斜率校验（剔除表面颗粒、刀纹及水平/垂直 IC 电路线条）
bool filterEdgeWithSlope(Points& points, int height, int view_idx, bool check_slope) {
    if (points.size() < 30) {
        return false;
    }
    Points retained = points;
    double c1 = 0.0;
    for (int iteration = 0; iteration < 4; ++iteration) {
        cv::Mat a(static_cast<int>(retained.size()), 3, CV_64F);
        cv::Mat b(static_cast<int>(retained.size()), 1, CV_64F);
        for (int j = 0; j < a.rows; ++j) {
            const double y = (retained[j].y - height * 0.5) / height;
            a.at<double>(j, 0) = 1.0;
            a.at<double>(j, 1) = y;
            a.at<double>(j, 2) = y * y;
            b.at<double>(j, 0) = retained[j].x;
        }
        cv::Mat c;
        if (!cv::solve(a, b, c, cv::DECOMP_SVD)) {
            return false;
        }
        const double c0 = c.at<double>(0);
        c1 = c.at<double>(1);
        const double c2 = c.at<double>(2);
        auto residual = [&](const cv::Point2d& p) {
            const double y = (p.y - height * 0.5) / height;
            return p.x - (c0 + c1 * y + c2 * y * y);
        };
        std::vector<double> errors;
        errors.reserve(retained.size());
        for (const auto& p : retained) {
            errors.push_back(residual(p));
        }
        const double median = quantile(errors, 0.5);
        for (double& e : errors) {
            e = std::abs(e - median);
        }
        const double limit = std::max(2.5, 3.5 * 1.4826 * quantile(errors, 0.5));
        retained.clear();
        for (const auto& p : points) {
            if (std::abs(residual(p) - median) < limit) {
                retained.push_back(p);
            }
        }
        if (retained.size() < 30) {
            return false;
        }
    }

    double min_y = retained.front().y;
    double max_y = retained.front().y;
    for (const auto& p : retained) {
        if (p.y < min_y) {
            min_y = p.y;
        }
        if (p.y > max_y) {
            max_y = p.y;
        }
    }
    if (max_y - min_y < height * 0.20) {
        return false;
    }

    if (check_slope) {
        const double slope = std::abs(c1 / static_cast<double>(height));
        if (view_idx < 4) {
            // 四角视野（45度方向）：晶圆圆弧切线斜率 |dx/dy| 理论值为 1.0
            if (slope < 0.45 || slope > 1.85) {
                return false;
            }
        } else {
            // 正交视野（0/90度方向，其中上下视野已转置）：晶圆圆弧切线斜率 |dx/dy| 理论值为 0.0
            if (slope > 0.35) {
                return false;
            }
        }
    }

    points.swap(retained);
    return true;
}

// 单行局部双窗口多候选亚像素边缘提取（同时兼容黑场、反光刀纹底座、暗场窄脉冲亮线及双圆弧去边环）
void extractRowCandidates(const std::vector<double>& row, std::vector<double>& cands) {
    cands.clear();
    const int w = static_cast<int>(row.size());
    int x = 60;
    while (x < w - 80) {
        if (row[x] < 42.0) {
            const int ahead_end = std::min(w - 40, x + 120);
            if (ahead_end > x + 4) {
                double max_ahead = row[x];
                for (int k = x; k < ahead_end; ++k) {
                    if (row[k] > max_ahead) {
                        max_ahead = row[k];
                    }
                }
                if (max_ahead - row[x] >= 8.0) {
                    int best_offset = 0;
                    double best_diff = -1.0;
                    for (int k = x; k + 4 < ahead_end; ++k) {
                        const double diff = row[k + 4] - row[k];
                        if (diff > best_diff) {
                            best_diff = diff;
                            best_offset = k - x;
                        }
                    }
                    const int xp = x + best_offset + 2;
                    const int b_start = std::max(20, xp - 120);
                    const int b_end = std::max(25, xp - 3);
                    const double b = slicePercentile(row, b_start, b_end, 0.15);

                    std::vector<double> dark_devs;
                    for (int k = b_start; k < b_end && k < w; ++k) {
                        if (row[k] <= b + 5.0) {
                            dark_devs.push_back(std::abs(row[k] - b));
                        }
                    }
                    const double sigma = (dark_devs.size() > 4) ? (1.4826 * quantile(dark_devs, 0.50)) : 1.0;

                    const double s_narrow = slicePercentile(row, xp, std::min(w - 20, xp + 35), 0.85);
                    const double s_medium = slicePercentile(row, std::min(w - 30, xp + 10), std::min(w - 20, xp + 100), 0.75);
                    const double s = std::max(s_narrow, s_medium);

                    const double min_contrast = (b < 10.0) ? 6.0 : 12.0;
                    if (b < 40.0 && (s - b) >= std::max(min_contrast, 4.0 * sigma) && s < 254.5) {
                        const double th = b + 0.15 * (s - b);
                        double sub_x = -1.0;
                        const int k_start = std::min(w - 25, xp + 20);
                        const int k_end = std::max(20, xp - 100);
                        for (int k = k_start; k > k_end; --k) {
                            if (row[k] <= th && row[k + 1] > th) {
                                const double denom = row[k + 1] - row[k];
                                if (denom > 1e-6) {
                                    sub_x = static_cast<double>(k) + (th - row[k]) / denom;
                                }
                                break;
                            }
                        }
                        if (sub_x >= 80.0 && sub_x <= static_cast<double>(w - 80)) {
                            if (cands.empty() || (sub_x - cands.back()) > 80.0) {
                                cands.push_back(sub_x);
                                x = static_cast<int>(sub_x) + 80;
                                continue;
                            }
                        }
                    }
                }
            }
        }
        x += 8;
    }
}

// 跨行 2D 连续圆弧链聚类提取（支持同一视野分离内外双圆弧并剔除杂散短链）
void extractCandidateArcs(
    const cv::Mat& image,
    int view_idx,
    bool scan_from_right,
    int density,
    std::vector<Points>& valid_arcs
) {
    valid_arcs.clear();
    cv::Mat smooth;
    image.convertTo(smooth, CV_32F);
    cv::GaussianBlur(smooth, smooth, cv::Size(), 2.0);

    const int width = smooth.cols;
    const int height = smooth.rows;
    const int margin = std::max(16, height / 32);
    const int step = std::max(1, height / density);
    const double max_dy = static_cast<double>(step) * 5.0;

    std::vector<Points> chains;
    std::vector<double> row(width);
    std::vector<double> cands;

    for (int y = margin; y < height - margin; y += step) {
        const float* source = smooth.ptr<float>(y);
        for (int x = 0; x < width; ++x) {
            row[x] = source[scan_from_right ? (width - 1 - x) : x];
        }
        extractRowCandidates(row, cands);

        for (double c : cands) {
            const double actual_x = scan_from_right ? (static_cast<double>(width - 1) - c) : c;
            int best_idx = -1;
            double best_dist = 35.0 * std::max(1.0, static_cast<double>(step) / 16.0);

            for (size_t idx = 0; idx < chains.size(); ++idx) {
                const auto& ch = chains[idx];
                const double last_x = ch.back().x;
                const double last_y = ch.back().y;
                const double dy = static_cast<double>(y) - last_y;
                if (dy > 0.0 && dy <= max_dy) {
                    double pred_x = last_x;
                    if (ch.size() >= 3) {
                        const double span_y = ch.back().y - ch[ch.size() - 3].y;
                        if (span_y > 0.0) {
                            const double dx_dy = (ch.back().x - ch[ch.size() - 3].x) / span_y;
                            pred_x = last_x + dx_dy * dy;
                        }
                    }
                    const double dist = std::abs(actual_x - pred_x);
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_idx = static_cast<int>(idx);
                    }
                }
            }

            if (best_idx >= 0) {
                chains[static_cast<size_t>(best_idx)].emplace_back(actual_x, static_cast<double>(y));
            } else {
                Points new_chain;
                new_chain.emplace_back(actual_x, static_cast<double>(y));
                chains.push_back(std::move(new_chain));
            }
        }
    }

    for (auto& ch : chains) {
        if (ch.size() >= 30) {
            Points candidate = ch;
            if (filterEdgeWithSlope(candidate, height, view_idx, true)) {
                valid_arcs.push_back(std::move(candidate));
            }
        }
    }
}

template<size_t N>
bool fitCircle(const std::array<Points, N>& groups, cv::Vec3d& circle) {
    size_t count = 0;
    for (const auto& group : groups) {
        if (group.empty()) {
            return false;
        }
        count += group.size();
    }
    if (count < 30) {
        return false;
    }

    cv::Point2d origin(0.0, 0.0);
    for (const auto& group : groups) {
        cv::Point2d mean(0.0, 0.0);
        for (const auto& p : group) {
            mean += p;
        }
        origin += mean * (1.0 / (static_cast<double>(N) * static_cast<double>(group.size())));
    }

    cv::Mat a(static_cast<int>(count), 3, CV_64F);
    cv::Mat b(static_cast<int>(count), 1, CV_64F);
    int row = 0;
    for (const auto& group : groups) {
        const double w = 1.0 / std::sqrt(static_cast<double>(group.size()));
        for (const auto& point : group) {
            const cv::Point2d p = point - origin;
            a.at<double>(row, 0) = 2.0 * p.x * w;
            a.at<double>(row, 1) = 2.0 * p.y * w;
            a.at<double>(row, 2) = w;
            b.at<double>(row, 0) = p.dot(p) * w;
            ++row;
        }
    }

    cv::Mat initial;
    if (!cv::solve(a, b, initial, cv::DECOMP_SVD)) {
        return false;
    }
    const double cx = initial.at<double>(0);
    const double cy = initial.at<double>(1);
    const double r2 = initial.at<double>(2) + cx * cx + cy * cy;
    if (!(r2 > 0.0) || !std::isfinite(r2)) {
        return false;
    }

    circle = cv::Vec3d(cx, cy, std::sqrt(r2));
    bool converged = false;
    for (int iteration = 0; iteration < 30; ++iteration) {
        row = 0;
        for (const auto& group : groups) {
            const double w = 1.0 / std::sqrt(static_cast<double>(group.size()));
            for (const auto& point : group) {
                const double x = circle[0] - (point.x - origin.x);
                const double y = circle[1] - (point.y - origin.y);
                const double d = std::hypot(x, y);
                if (d < 1e-12 || !std::isfinite(d)) {
                    return false;
                }
                a.at<double>(row, 0) = x / d * w;
                a.at<double>(row, 1) = y / d * w;
                a.at<double>(row, 2) = -w;
                b.at<double>(row, 0) = (circle[2] - d) * w;
                ++row;
            }
        }
        cv::Mat delta;
        if (!cv::solve(a, b, delta, cv::DECOMP_SVD)) {
            return false;
        }
        for (int j = 0; j < 3; ++j) {
            circle[j] += delta.at<double>(j);
        }
        if (cv::norm(delta) < 1e-10) {
            converged = true;
            break;
        }
    }
    circle[0] += origin.x;
    circle[1] += origin.y;
    return converged && std::isfinite(circle[0]) && std::isfinite(circle[1]) &&
           std::isfinite(circle[2]) && circle[2] > 0.0;
}

cv::Point2d toWorld(const cv::Point2d& p, const double* stage, int width, int height, double scale) {
    return {
        stage[0] + (p.x - width * 0.5) * scale,
        stage[1] - (p.y - height * 0.5) * scale
    };
}

// 多视野全局共圆一致性优选：按径向由外向内排序，自动分离去边环内圆弧并锁定真实晶圆外圆周
template<size_t N>
bool selectBestOuterArcs(
    const std::array<std::vector<Points>, N>& view_arcs_world,
    const double* stage_xy_mm,
    std::array<Points, N>& selected,
    cv::Vec3d& circle
) {
    double cx_approx = 0.0;
    double cy_approx = 0.0;
    for (int i = 0; i < 4; ++i) {
        cx_approx += stage_xy_mm[2 * i] * 0.25;
        cy_approx += stage_xy_mm[2 * i + 1] * 0.25;
    }

    std::array<std::vector<std::pair<double, Points>>, N> sorted_arcs;
    for (size_t i = 0; i < N; ++i) {
        if (view_arcs_world[i].empty()) {
            return false;
        }
        for (const auto& arc : view_arcs_world[i]) {
            double sum_r = 0.0;
            for (const auto& p : arc) {
                sum_r += std::hypot(p.x - cx_approx, p.y - cy_approx);
            }
            const double mean_r = sum_r / static_cast<double>(arc.size());
            sorted_arcs[i].emplace_back(mean_r, arc);
        }
        std::sort(
            sorted_arcs[i].begin(),
            sorted_arcs[i].end(),
            [](const std::pair<double, Points>& a, const std::pair<double, Points>& b) {
                return a.first > b.first;
            }
        );
        selected[i] = sorted_arcs[i][0].second;
    }

    if (!fitCircle(selected, circle)) {
        return false;
    }

    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < N; ++i) {
            if (sorted_arcs[i].size() > 1) {
                Points best_arc = selected[i];
                std::vector<double> errs;
                for (const auto& p : best_arc) {
                    errs.push_back(std::abs(std::hypot(p.x - circle[0], p.y - circle[1]) - circle[2]));
                }
                double best_err = quantile(errs, 0.50);

                for (const auto& item : sorted_arcs[i]) {
                    std::vector<double> cand_errs;
                    for (const auto& p : item.second) {
                        cand_errs.push_back(std::abs(std::hypot(p.x - circle[0], p.y - circle[1]) - circle[2]));
                    }
                    const double err = quantile(cand_errs, 0.50);
                    if (err < best_err * 0.5 && err < 0.2) {
                        best_err = err;
                        best_arc = item.second;
                    }
                }
                selected[i] = std::move(best_arc);
            }
        }
        if (!fitCircle(selected, circle)) {
            return false;
        }
    }

    return true;
}

double notchAngle(
    const Points& edge,
    const double* stage,
    int width,
    int height,
    double scale,
    const cv::Vec3d& circle
) {
    std::vector<double> angles;
    std::vector<double> depths;
    std::vector<double> midpoints;
    angles.reserve(edge.size());
    depths.reserve(edge.size());

    for (const auto& p : edge) {
        const auto q = toWorld(p, stage, width, height, scale);
        angles.push_back(std::atan2(q.x - circle[0], circle[1] - q.y) * 180.0 / CV_PI);
        depths.push_back(circle[2] - std::hypot(q.x - circle[0], q.y - circle[1]));
    }

    // 在 0.3mm ~ 1.0mm 的等深截面上计算左右交点中位角
    for (int section = 3; section <= 10; ++section) {
        const double level = section * 0.1;
        std::vector<double> crossings;
        for (size_t j = 0; j + 1 < edge.size(); ++j) {
            if ((depths[j] <= level && depths[j + 1] > level) ||
                (depths[j] > level && depths[j + 1] <= level)) {
                if (std::abs(edge[j + 1].x - edge[j].x) > 8.0 * std::max(1, width / 1024)) {
                    continue;
                }
                crossings.push_back(
                    angles[j] + (level - depths[j]) / (depths[j + 1] - depths[j]) * (angles[j + 1] - angles[j])
                );
            }
        }
        if (crossings.size() == 2 && std::isfinite(crossings[0]) && std::isfinite(crossings[1])) {
            midpoints.push_back((crossings[0] + crossings[1]) * 0.5);
        }
    }
    if (midpoints.size() < 3) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::sort(midpoints.begin(), midpoints.end());
    const size_t n = midpoints.size();
    return (midpoints[(n - 1) / 2] + midpoints[n / 2]) * 0.5;
}
} // namespace

extern "C" WAFER_API int Wafer_FindFourViewEdgeCircle(
    const unsigned char** image_buffers,
    const double* stage_xy_mm,
    int width,
    int height,
    double pixel_size_um,
    WaferEdgeCircleResult* result
) {
    if (!result) {
        return WAFER_ERR_INVALID_PARAM;
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    *result = {nan, nan, nan, nan, nan};

    if (!image_buffers || !stage_xy_mm || width < 512 || height < 256 ||
        !std::isfinite(pixel_size_um) || pixel_size_um <= 0.0 ||
        static_cast<size_t>(width) > static_cast<size_t>(std::numeric_limits<int>::max()) / height) {
        return WAFER_ERR_INVALID_PARAM;
    }
    for (int i = 0; i < 4; ++i) {
        if (!image_buffers[i] || !std::isfinite(stage_xy_mm[2 * i]) || !std::isfinite(stage_xy_mm[2 * i + 1])) {
            return WAFER_ERR_INVALID_PARAM;
        }
    }
    if (!(stage_xy_mm[0] > stage_xy_mm[4] && stage_xy_mm[2] > stage_xy_mm[6] &&
          stage_xy_mm[1] > stage_xy_mm[3] && stage_xy_mm[5] > stage_xy_mm[7])) {
        return WAFER_ERR_INVALID_PARAM;
    }

    try {
        const double scale = pixel_size_um / 1000.0;
        std::array<std::vector<Points>, 4> view_arcs_world;

        for (int i = 0; i < 4; ++i) {
            cv::Mat input(height, width, CV_8UC1, const_cast<unsigned char*>(image_buffers[i]));
            const bool scan_from_right = (i < 2);
            std::vector<Points> arcs;
            extractCandidateArcs(input, i, scan_from_right, 256, arcs);
            if (arcs.empty()) {
                extractCandidateArcs(input, i, !scan_from_right, 256, arcs);
            }
            if (arcs.empty()) {
                return WAFER_ERR_FEATURE_NOT_FOUND;
            }
            for (const auto& arc : arcs) {
                Points world_pts;
                world_pts.reserve(arc.size());
                for (const auto& p : arc) {
                    world_pts.push_back(toWorld(p, stage_xy_mm + 2 * i, width, height, scale));
                }
                view_arcs_world[i].push_back(std::move(world_pts));
            }
        }

        std::array<Points, 4> groups;
        cv::Vec3d circle;
        if (!selectBestOuterArcs(view_arcs_world, stage_xy_mm, groups, circle)) {
            return WAFER_ERR_FITTING_FAILED;
        }

        for (int i = 0; i < 4; ++i) {
            Points clean_group;
            clean_group.reserve(groups[i].size());
            for (const auto& p : groups[i]) {
                const double d = std::hypot(p.x - circle[0], p.y - circle[1]);
                if (std::abs(d - circle[2]) < 1.5) {
                    clean_group.push_back(p);
                }
            }
            if (clean_group.size() < 30) {
                return WAFER_ERR_FEATURE_NOT_FOUND;
            }
            groups[i] = std::move(clean_group);
        }
        if (!fitCircle(groups, circle)) {
            return WAFER_ERR_FITTING_FAILED;
        }

        double mse = 0.0;
        double maximum = 0.0;
        for (const auto& group : groups) {
            double sum = 0.0;
            for (const auto& p : group) {
                const double e = std::hypot(p.x - circle[0], p.y - circle[1]) - circle[2];
                sum += e * e;
                maximum = std::max(maximum, std::abs(e));
            }
            mse += sum / (4.0 * static_cast<double>(group.size()));
        }
        const double rms = std::sqrt(mse) * 1000.0;
        if (!std::isfinite(rms) || rms > 100.0 * pixel_size_um) {
            return WAFER_ERR_FITTING_FAILED;
        }

        *result = {circle[0], circle[1], circle[2], rms, maximum * 1000.0};
        return WAFER_SUCCESS;
    } catch (const cv::Exception&) {
        return WAFER_ERR_UNKNOWN;
    } catch (...) {
        return WAFER_ERR_UNKNOWN;
    }
}

extern "C" WAFER_API int Wafer_FindEightViewEdgeCircle(
    const unsigned char** image_buffers,
    const double* stage_xy_mm,
    int width,
    int height,
    double pixel_size_um,
    WaferEightViewEdgeCircleResult* result
) {
    if (!result) {
        return WAFER_ERR_INVALID_PARAM;
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    *result = {nan, nan, nan, nan, nan, nan};

    if (!image_buffers || !stage_xy_mm || width < 512 || height < 512 ||
        !std::isfinite(pixel_size_um) || pixel_size_um <= 0.0 ||
        static_cast<size_t>(width) > static_cast<size_t>(std::numeric_limits<int>::max()) / height) {
        return WAFER_ERR_INVALID_PARAM;
    }
    for (int i = 0; i < 8; ++i) {
        if (!image_buffers[i] || !std::isfinite(stage_xy_mm[2 * i]) || !std::isfinite(stage_xy_mm[2 * i + 1])) {
            return WAFER_ERR_INVALID_PARAM;
        }
    }
    if (!(stage_xy_mm[0] > stage_xy_mm[4] && stage_xy_mm[2] > stage_xy_mm[6] &&
          stage_xy_mm[1] > stage_xy_mm[3] && stage_xy_mm[5] > stage_xy_mm[7] &&
          stage_xy_mm[8] > stage_xy_mm[10] && stage_xy_mm[13] > stage_xy_mm[15])) {
        return WAFER_ERR_INVALID_PARAM;
    }

    try {
        const double scale = pixel_size_um / 1000.0;
        std::array<std::vector<Points>, 7> view_arcs_world;

        for (int i = 0; i < 7; ++i) {
            cv::Mat input(height, width, CV_8UC1, const_cast<unsigned char*>(image_buffers[i]));
            cv::Mat scan;
            if (i == 6) {
                cv::transpose(input, scan);
            } else {
                scan = input;
            }
            const bool scan_from_right = (i < 2 || i == 4);
            std::vector<Points> arcs;
            extractCandidateArcs(scan, i, scan_from_right, 256, arcs);
            if (arcs.empty()) {
                extractCandidateArcs(scan, i, !scan_from_right, 256, arcs);
            }
            if (arcs.empty()) {
                return WAFER_ERR_FEATURE_NOT_FOUND;
            }
            for (const auto& arc : arcs) {
                Points world_pts;
                world_pts.reserve(arc.size());
                for (const auto& p : arc) {
                    const cv::Point2d orig_p = (i == 6) ? cv::Point2d(p.y, p.x) : p;
                    world_pts.push_back(toWorld(orig_p, stage_xy_mm + 2 * i, width, height, scale));
                }
                view_arcs_world[i].push_back(std::move(world_pts));
            }
        }

        std::array<Points, 7> seedGroups;
        cv::Vec3d circle;
        if (!selectBestOuterArcs(view_arcs_world, stage_xy_mm, seedGroups, circle)) {
            return WAFER_ERR_FITTING_FAILED;
        }

        for (int i = 0; i < 7; ++i) {
            Points clean_group;
            clean_group.reserve(seedGroups[i].size());
            for (const auto& p : seedGroups[i]) {
                const double d = std::hypot(p.x - circle[0], p.y - circle[1]);
                if (std::abs(d - circle[2]) < 1.5) {
                    clean_group.push_back(p);
                }
            }
            if (clean_group.size() < 30) {
                return WAFER_ERR_FEATURE_NOT_FOUND;
            }
            seedGroups[i] = std::move(clean_group);
        }
        if (!fitCircle(seedGroups, circle)) {
            return WAFER_ERR_FITTING_FAILED;
        }

        // 第 8 视野（View 7，位于 -Y 正交方向，含 Notch 缺口）：
        // 利用前 7 视野求出的高精度基准圆施加物理深度窗 [-0.25, +1.35] mm，并通过连续链聚类提取纯净轮廓
        cv::Mat input7(height, width, CV_8UC1, const_cast<unsigned char*>(image_buffers[7]));
        cv::Mat scan7;
        cv::transpose(input7, scan7);
        cv::Mat smooth7;
        scan7.convertTo(smooth7, CV_32F);
        cv::GaussianBlur(smooth7, smooth7, cv::Size(), 2.0);

        const int sh = smooth7.rows;
        const int sw = smooth7.cols;
        const int margin7 = std::max(16, sh / 32);
        const int step7 = std::max(1, sh / 1024);
        const double max_dy7 = static_cast<double>(step7) * 12.0;

        std::vector<Points> chains7;
        std::vector<double> row7(sw);
        std::vector<double> cands7;

        for (int y = margin7; y < sh - margin7; y += step7) {
            const float* src = smooth7.ptr<float>(y);
            for (int x = 0; x < sw; ++x) {
                row7[x] = src[sw - 1 - x];
            }
            extractRowCandidates(row7, cands7);

            for (double c : cands7) {
                const double actual_x = static_cast<double>(sw - 1) - c;
                const cv::Point2d orig_p(static_cast<double>(y), actual_x);
                const cv::Point2d q = toWorld(orig_p, stage_xy_mm + 14, width, height, scale);
                const double depth = circle[2] - std::hypot(q.x - circle[0], q.y - circle[1]);

                if (depth >= -0.25 && depth <= 1.35) {
                    int best_idx = -1;
                    double best_dist = 45.0 * std::max(1.0, static_cast<double>(step7) / 8.0);
                    for (size_t idx = 0; idx < chains7.size(); ++idx) {
                        const auto& ch = chains7[idx];
                        const double last_x = ch.back().x;
                        const double last_y = ch.back().y;
                        const double dy = static_cast<double>(y) - last_y;
                        if (dy > 0.0 && dy <= max_dy7) {
                            double pred_x = last_x;
                            if (ch.size() >= 3) {
                                const double span_y = ch.back().y - ch[ch.size() - 3].y;
                                if (span_y > 0.0) {
                                    const double dx_dy = (ch.back().x - ch[ch.size() - 3].x) / span_y;
                                    pred_x = last_x + dx_dy * dy;
                                }
                            }
                            const double dist = std::abs(actual_x - pred_x);
                            if (dist < best_dist) {
                                best_dist = dist;
                                best_idx = static_cast<int>(idx);
                            }
                        }
                    }
                    if (best_idx >= 0) {
                        chains7[static_cast<size_t>(best_idx)].emplace_back(actual_x, static_cast<double>(y));
                    } else {
                        Points new_chain;
                        new_chain.emplace_back(actual_x, static_cast<double>(y));
                        chains7.push_back(std::move(new_chain));
                    }
                }
            }
        }

        // 汇总长度 >= 15 的有效连续轮廓链并按原图横坐标排序（自动剔除 Notch 内部孤立微尘噪点）
        Points raw;
        for (const auto& ch : chains7) {
            if (ch.size() >= 15) {
                for (const auto& pt : ch) {
                    const double orig_x = pt.y;
                    const double orig_y = pt.x;
                    bool exists = false;
                    for (const auto& existing : raw) {
                        if (std::abs(existing.x - orig_x) < 1e-6) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists) {
                        raw.emplace_back(orig_x, orig_y);
                    }
                }
            }
        }
        std::sort(
            raw.begin(),
            raw.end(),
            [](const cv::Point2d& a, const cv::Point2d& b) {
                return a.x < b.x;
            }
        );

        if (raw.size() < 30) {
            return WAFER_ERR_FEATURE_NOT_FOUND;
        }

        double left = static_cast<double>(width);
        double right = -1.0;
        for (const auto& p : raw) {
            const auto q = toWorld(p, stage_xy_mm + 14, width, height, scale);
            if (circle[2] - std::hypot(q.x - circle[0], q.y - circle[1]) > std::max(0.1, 10.0 * scale)) {
                left = std::min(left, p.x);
                right = std::max(right, p.x);
            }
        }

        Points normal;
        for (const auto& p : raw) {
            if (right < 0.0 || p.x < left - 60.0 || p.x > right + 60.0) {
                normal.emplace_back(p.y, p.x);
            }
        }
        if (!filterEdgeWithSlope(normal, width, 7, true)) {
            return WAFER_ERR_FEATURE_NOT_FOUND;
        }

        std::array<Points, 8> groups;
        for (int i = 0; i < 7; ++i) {
            groups[i] = std::move(seedGroups[i]);
        }
        for (const auto& p : normal) {
            groups[7].push_back(toWorld({p.y, p.x}, stage_xy_mm + 14, width, height, scale));
        }
        if (!fitCircle(groups, circle)) {
            return WAFER_ERR_FITTING_FAILED;
        }

        double mse = 0.0;
        double maximum = 0.0;
        for (const auto& group : groups) {
            double sum = 0.0;
            for (const auto& p : group) {
                const double e = std::hypot(p.x - circle[0], p.y - circle[1]) - circle[2];
                sum += e * e;
                maximum = std::max(maximum, std::abs(e));
            }
            mse += sum / (8.0 * static_cast<double>(group.size()));
        }
        const double rms = std::sqrt(mse) * 1000.0;
        if (!std::isfinite(rms) || rms > 100.0 * pixel_size_um) {
            return WAFER_ERR_FITTING_FAILED;
        }
        const double angle = (right < 0.0) ? nan : notchAngle(raw, stage_xy_mm + 14, width, height, scale, circle);
        *result = {circle[0], circle[1], circle[2], rms, maximum * 1000.0, angle};
        return WAFER_SUCCESS;
    } catch (...) {
        return WAFER_ERR_UNKNOWN;
    }
}
