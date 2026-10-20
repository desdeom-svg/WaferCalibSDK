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

double quantile(std::vector<double> values, double fraction) {
    const size_t k = static_cast<size_t>(fraction * (values.size() - 1));
    std::nth_element(values.begin(), values.begin() + k, values.end());
    return values[k];
}

// Fit local edge geometry only to reject particles, never to extrapolate a circle center.
bool filterEdge(Points& points, int height) {
    if (points.size() < 30) return false;
    Points retained = points;
    for (int iteration = 0; iteration < 4; ++iteration) {
        cv::Mat a(static_cast<int>(retained.size()), 3, CV_64F);
        cv::Mat b(static_cast<int>(retained.size()), 1, CV_64F);
        for (int j = 0; j < a.rows; ++j) {
            const double y = (retained[j].y - height * .5) / height;
            a.at<double>(j, 0) = 1.; a.at<double>(j, 1) = y; a.at<double>(j, 2) = y * y;
            b.at<double>(j) = retained[j].x;
        }
        cv::Mat c;
        if (!cv::solve(a, b, c, cv::DECOMP_SVD)) return false;
        auto residual = [&](const cv::Point2d& p) {
            const double y = (p.y - height * .5) / height;
            return p.x - (c.at<double>(0) + c.at<double>(1) * y + c.at<double>(2) * y * y);
        };
        std::vector<double> errors;
        for (const auto& p : retained) errors.push_back(residual(p));
        const double median = quantile(errors, .5);
        for (double& e : errors) e = std::abs(e - median);
        const double limit = std::max(3., 4. * 1.4826 * quantile(errors, .5));
        retained.clear();
        for (const auto& p : points)
            if (std::abs(residual(p) - median) < limit) retained.push_back(p);
        if (retained.size() < 30) return false;
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
    points.swap(retained);
    return true;
}

bool extractEdgeDirectional(
    const cv::Mat& smooth,
    bool scan_from_right,
    bool filter,
    int density,
    Points& points
) {
    points.clear();
    const int width = smooth.cols;
    const int height = smooth.rows;
    const int margin = std::max(16, height / 32);
    const int step = std::max(1, height / density);
    std::vector<double> row(width);

    for (int y = margin; y < height - margin; y += step) {
        const float* source = smooth.ptr<float>(y);
        for (int x = 0; x < width; ++x) {
            row[x] = source[scan_from_right ? width - 1 - x : x];
        }

        const double bg = quantile(row, 0.10);
        const double signal = quantile(row, 0.85);
        if (signal - bg < 5.0) {
            continue;
        }

        const double coarseThreshold = bg + 0.40 * (signal - bg);
        int run = 0;
        int coarse = -1;
        for (int x = 0; x < width; ++x) {
            run = (row[x] > coarseThreshold) ? (run + 1) : 0;
            if (run == 15) {
                coarse = x - 14;
                break;
            }
        }
        if (coarse < 130 || coarse >= width - 130) {
            continue;
        }

        std::vector<double> exterior(row.begin() + coarse - 120, row.begin() + coarse - 60);
        std::vector<double> interior(row.begin() + coarse + 30, row.begin() + coarse + 100);
        const double b = quantile(exterior, 0.50);
        const double s = quantile(interior, 0.50);
        if (s - b < 5.0) {
            continue;
        }

        std::vector<double> noise = exterior;
        for (double& value : noise) {
            value = std::abs(value - b);
        }
        const double sigma = 1.4826 * quantile(noise, 0.50);
        const double threshold = b + 0.15 * (s - b);

        if (threshold - b < 3.0 * sigma || s >= 254.0) {
            continue;
        }

        for (int x = coarse - 100; x < coarse + 30; ++x) {
            if (row[x] <= threshold && row[x + 1] > threshold) {
                const double subpixel = x + (threshold - row[x]) / (row[x + 1] - row[x]);
                const double actual_x = scan_from_right ? (width - 1.0 - subpixel) : subpixel;
                points.emplace_back(actual_x, static_cast<double>(y));
                break;
            }
        }
    }

    if (filter) {
        return filterEdge(points, height);
    }
    return points.size() >= 30;
}

bool detectEdge(const cv::Mat& image, int view, Points& points, bool filter = true, int density = 256) {
    cv::Mat smooth;
    image.convertTo(smooth, CV_32F);
    cv::GaussianBlur(smooth, smooth, cv::Size(), 2.0);

    const int width = smooth.cols;
    const int height = smooth.rows;

    // 自动采样图像左右边缘区域灰度，自适应探测黑场位于左侧还是右侧
    const int check_margin = std::max(16, height / 32);
    const int check_cols = std::min(64, width / 8);
    std::vector<double> left_samples;
    std::vector<double> right_samples;
    left_samples.reserve(static_cast<size_t>((height - 2 * check_margin) / 8 * check_cols));
    right_samples.reserve(static_cast<size_t>((height - 2 * check_margin) / 8 * check_cols));

    for (int y = check_margin; y < height - check_margin; y += 8) {
        const float* ptr = smooth.ptr<float>(y);
        for (int x = 0; x < check_cols; ++x) {
            left_samples.push_back(ptr[x]);
            right_samples.push_back(ptr[width - 1 - x]);
        }
    }

    const double left_median = quantile(left_samples, 0.50);
    const double right_median = quantile(right_samples, 0.50);

    // 默认或优先建议方向：若左侧明显较暗，则黑场在左侧（scan_from_right = false）；
    // 若右侧明显较暗，则黑场在右侧（scan_from_right = true）；
    // 若两侧差异不显著，则参考原入参 view < 2 作为初选建议
    bool preferred_scan_from_right = (view < 2);
    if (std::abs(right_median - left_median) >= 5.0) {
        preferred_scan_from_right = (right_median < left_median);
    }

    // 1. 首选自适应方向尝试提取
    Points cand_pts;
    bool success = extractEdgeDirectional(smooth, preferred_scan_from_right, filter, density, cand_pts);

    // 2. 自适应回退机制：若首选方向提取失败，自动切换为反向扫描重试，彻底免疫相机镜像
    if (!success) {
        Points fallback_pts;
        bool fallback_success = extractEdgeDirectional(smooth, !preferred_scan_from_right, filter, density, fallback_pts);
        if (fallback_success) {
            points = std::move(fallback_pts);
            return true;
        }
        return false;
    }

    points = std::move(cand_pts);
    return true;
}

template<size_t N>
bool fitCircle(const std::array<Points, N>& groups, cv::Vec3d& circle) {
    size_t count = 0;
    for (const auto& group : groups) count += group.size();
    cv::Point2d origin;
    for (const auto& group : groups) {
        cv::Point2d mean;
        for (const auto& p : group) mean += p;
        origin += mean * (1. / (N * group.size()));
    }
    cv::Mat a(static_cast<int>(count), 3, CV_64F), b(static_cast<int>(count), 1, CV_64F);
    int row = 0;
    for (const auto& group : groups) {
        const double w = 1. / std::sqrt(static_cast<double>(group.size()));
        for (const auto& point : group) {
            const cv::Point2d p = point - origin;
            a.at<double>(row, 0) = 2. * p.x * w; a.at<double>(row, 1) = 2. * p.y * w;
            a.at<double>(row, 2) = w; b.at<double>(row) = p.dot(p) * w; ++row;
        }
    }
    cv::Mat initial;
    if (!cv::solve(a, b, initial, cv::DECOMP_SVD)) return false;
    const double cx = initial.at<double>(0), cy = initial.at<double>(1);
    const double r2 = initial.at<double>(2) + cx * cx + cy * cy;
    if (!(r2 > 0.) || !std::isfinite(r2)) return false;
    circle = cv::Vec3d(cx, cy, std::sqrt(r2));
    bool converged = false;
    for (int iteration = 0; iteration < 30; ++iteration) {
        row = 0;
        for (const auto& group : groups) {
            const double w = 1. / std::sqrt(static_cast<double>(group.size()));
            for (const auto& point : group) {
                const double x = circle[0] - (point.x - origin.x);
                const double y = circle[1] - (point.y - origin.y);
                const double d = std::hypot(x, y);
                if (d < 1e-12 || !std::isfinite(d)) return false;
                a.at<double>(row, 0) = x / d * w; a.at<double>(row, 1) = y / d * w;
                a.at<double>(row, 2) = -w; b.at<double>(row) = (circle[2] - d) * w; ++row;
            }
        }
        cv::Mat delta;
        if (!cv::solve(a, b, delta, cv::DECOMP_SVD)) return false;
        for (int j = 0; j < 3; ++j) circle[j] += delta.at<double>(j);
        if (cv::norm(delta) < 1e-10) { converged = true; break; }
    }
    circle[0] += origin.x; circle[1] += origin.y;
    return converged && std::isfinite(circle[0]) && std::isfinite(circle[1]) &&
        std::isfinite(circle[2]) && circle[2] > 0.;
}

cv::Point2d toWorld(const cv::Point2d& p, const double* stage, int width, int height, double scale) {
    return {stage[0] + (p.x - width * .5) * scale,
            stage[1] - (p.y - height * .5) * scale};
}

double notchAngle(const Points& edge, const double* stage, int width, int height,
                  double scale, const cv::Vec3d& circle) {
    std::vector<double> angles, depths, midpoints;
    for (const auto& p : edge) {
        const auto q = toWorld(p, stage, width, height, scale);
        angles.push_back(std::atan2(q.x - circle[0], circle[1] - q.y) * 180. / CV_PI);
        depths.push_back(circle[2] - std::hypot(q.x - circle[0], q.y - circle[1]));
    }
    // shortcut: depth sections suit the current ~1 mm notch; smaller notches return NaN.
    for (int section = 3; section <= 10; ++section) {
        const double level = section * .1;
        std::vector<double> crossings;
        for (size_t j = 0; j + 1 < edge.size(); ++j) {
            if ((depths[j] <= level && depths[j+1] > level) ||
                (depths[j] > level && depths[j+1] <= level)) {
                // Do not bridge missing edge detections across a reflection or particle.
                if (edge[j+1].x - edge[j].x > 8. * std::max(1, width / 1024)) {
                    crossings.push_back(std::numeric_limits<double>::quiet_NaN());
                    continue;
                }
                crossings.push_back(angles[j] + (level - depths[j]) /
                    (depths[j+1] - depths[j]) * (angles[j+1] - angles[j]));
            }
        }
        if (crossings.size() == 2 && std::isfinite(crossings[0]) && std::isfinite(crossings[1]))
            midpoints.push_back((crossings[0] + crossings[1]) * .5);
    }
    if (midpoints.size() < 3) return std::numeric_limits<double>::quiet_NaN();
    std::sort(midpoints.begin(), midpoints.end());
    const size_t n = midpoints.size();
    return (midpoints[(n-1)/2] + midpoints[n/2]) * .5;
}
} // namespace

extern "C" WAFER_API int Wafer_FindFourViewEdgeCircle(
    const unsigned char** image_buffers, const double* stage_xy_mm,
    int width, int height, double pixel_size_um, WaferEdgeCircleResult* result) {
    if (!result) return WAFER_ERR_INVALID_PARAM;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    *result = {nan, nan, nan, nan, nan};
    if (!image_buffers || !stage_xy_mm || width < 512 || height < 256 ||
        !std::isfinite(pixel_size_um) || pixel_size_um <= 0. ||
        static_cast<size_t>(width) > static_cast<size_t>(std::numeric_limits<int>::max()) / height)
        return WAFER_ERR_INVALID_PARAM;
    for (int i = 0; i < 4; ++i) {
        if (!image_buffers[i] || !std::isfinite(stage_xy_mm[2*i]) || !std::isfinite(stage_xy_mm[2*i+1]))
            return WAFER_ERR_INVALID_PARAM;
    }
    // Fixed acquisition quadrants are part of this six-parameter ABI.
    if (!(stage_xy_mm[0] > stage_xy_mm[4] && stage_xy_mm[2] > stage_xy_mm[6] &&
          stage_xy_mm[1] > stage_xy_mm[3] && stage_xy_mm[5] > stage_xy_mm[7]))
        return WAFER_ERR_INVALID_PARAM;
    try {
        std::array<Points, 4> groups;
        const double scale = pixel_size_um / 1000.;
        for (int i = 0; i < 4; ++i) {
            cv::Mat input(height, width, CV_8UC1, const_cast<unsigned char*>(image_buffers[i]));
            if (!detectEdge(input, i, groups[i])) return WAFER_ERR_FEATURE_NOT_FOUND;
            for (auto& p : groups[i]) {
                p.x = stage_xy_mm[2*i] + (p.x - width * .5) * scale;
                p.y = stage_xy_mm[2*i+1] - (p.y - height * .5) * scale;
            }
        }
        cv::Vec3d circle;
        if (!fitCircle(groups, circle)) return WAFER_ERR_FITTING_FAILED;
        double mse = 0., maximum = 0.;
        for (const auto& group : groups) {
            double sum = 0.;
            for (const auto& p : group) {
                const double e = std::hypot(p.x - circle[0], p.y - circle[1]) - circle[2];
                sum += e * e; maximum = std::max(maximum, std::abs(e));
            }
            mse += sum / (4. * group.size());
        }
        const double rms = std::sqrt(mse) * 1000.;
        if (!std::isfinite(rms) || rms > 100. * pixel_size_um) {
            return WAFER_ERR_FITTING_FAILED;
        }
        *result = {circle[0], circle[1], circle[2], rms, maximum * 1000.};
        return WAFER_SUCCESS;
    } catch (const cv::Exception&) {
        return WAFER_ERR_UNKNOWN;
    } catch (...) {
        return WAFER_ERR_UNKNOWN;
    }
}

extern "C" WAFER_API int Wafer_FindEightViewEdgeCircle(
    const unsigned char** image_buffers, const double* stage_xy_mm,
    int width, int height, double pixel_size_um, WaferEightViewEdgeCircleResult* result) {
    if (!result) return WAFER_ERR_INVALID_PARAM;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    *result = {nan, nan, nan, nan, nan, nan};
    if (!image_buffers || !stage_xy_mm || width < 512 || height < 512 ||
        !std::isfinite(pixel_size_um) || pixel_size_um <= 0. ||
        static_cast<size_t>(width) > static_cast<size_t>(std::numeric_limits<int>::max()) / height)
        return WAFER_ERR_INVALID_PARAM;
    for (int i = 0; i < 8; ++i)
        if (!image_buffers[i] || !std::isfinite(stage_xy_mm[2*i]) || !std::isfinite(stage_xy_mm[2*i+1]))
            return WAFER_ERR_INVALID_PARAM;
    if (!(stage_xy_mm[0] > stage_xy_mm[4] && stage_xy_mm[2] > stage_xy_mm[6] &&
          stage_xy_mm[1] > stage_xy_mm[3] && stage_xy_mm[5] > stage_xy_mm[7] &&
          stage_xy_mm[8] > stage_xy_mm[10] && stage_xy_mm[13] > stage_xy_mm[15]))
        return WAFER_ERR_INVALID_PARAM;
    try {
        const double scale = pixel_size_um / 1000.;
        std::array<Points, 7> seedGroups;
        for (int i = 0; i < 7; ++i) {
            cv::Mat input(height, width, CV_8UC1, const_cast<unsigned char*>(image_buffers[i]));
            cv::Mat scan;
            if (i == 6) cv::transpose(input, scan);
            else scan = input;
            const int direction = (i < 2 || i == 4) ? 0 : 2;
            if (!detectEdge(scan, direction, seedGroups[i])) return WAFER_ERR_FEATURE_NOT_FOUND;
            for (auto& p : seedGroups[i]) {
                if (i == 6) std::swap(p.x, p.y);
                p = toWorld(p, stage_xy_mm + 2*i, width, height, scale);
            }
        }
        cv::Vec3d circle;
        if (!fitCircle(seedGroups, circle)) return WAFER_ERR_FITTING_FAILED;
        cv::Mat input(height, width, CV_8UC1, const_cast<unsigned char*>(image_buffers[7])), scan;
        cv::transpose(input, scan);
        Points raw, normal;
        if (!detectEdge(scan, 0, raw, false, 1024)) return WAFER_ERR_FEATURE_NOT_FOUND;
        double left = width, right = -1.;
        for (auto& p : raw) {
            std::swap(p.x, p.y);
            const auto q = toWorld(p, stage_xy_mm + 14, width, height, scale);
            if (circle[2] - std::hypot(q.x - circle[0], q.y - circle[1]) > std::max(.1, 10. * scale)) {
                left = std::min(left, p.x); right = std::max(right, p.x);
            }
        }
        for (const auto& p : raw)
            if (right < 0. || p.x < left - 60. || p.x > right + 60.)
                normal.emplace_back(p.y, p.x);
        if (!filterEdge(normal, width)) return WAFER_ERR_FEATURE_NOT_FOUND;
        std::array<Points, 8> groups;
        for (int i = 0; i < 7; ++i) groups[i] = std::move(seedGroups[i]);
        for (const auto& p : normal)
            groups[7].push_back(toWorld({p.y, p.x}, stage_xy_mm + 14, width, height, scale));
        if (!fitCircle(groups, circle)) return WAFER_ERR_FITTING_FAILED;
        double mse = 0., maximum = 0.;
        for (const auto& group : groups) {
            double sum = 0.;
            for (const auto& p : group) {
                const double e = std::hypot(p.x - circle[0], p.y - circle[1]) - circle[2];
                sum += e * e; maximum = std::max(maximum, std::abs(e));
            }
            mse += sum / (8. * group.size());
        }
        const double rms = std::sqrt(mse) * 1000.;
        if (!std::isfinite(rms) || rms > 100. * pixel_size_um) {
            return WAFER_ERR_FITTING_FAILED;
        }
        const double angle = right < 0. ? nan : notchAngle(raw, stage_xy_mm + 14, width, height, scale, circle);
        *result = {circle[0], circle[1], circle[2], rms, maximum * 1000., angle};
        return WAFER_SUCCESS;
    } catch (...) {
        return WAFER_ERR_UNKNOWN;
    }
}
