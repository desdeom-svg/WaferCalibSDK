#include <cmath>
#include <numeric>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include "wafer_calib/c_api/wafer_calib_c.h"
#include "wafer_calib/modules/distortion_correction.hpp"

namespace {

cv::Ptr<cv::FeatureDetector> createDarkCircleDetector() {
    cv::SimpleBlobDetector::Params params;
    params.minThreshold = 0.0F;
    params.maxThreshold = 255.0F;
    params.thresholdStep = 5.0F;
    params.filterByColor = true;
    params.blobColor = 0;
    params.filterByArea = true;
    params.minArea = 50.0F;
    params.maxArea = 300.0F;
    params.filterByCircularity = true;
    params.minCircularity = 0.7F;
    params.filterByConvexity = false;
    params.filterByInertia = false;
    params.minDistBetweenBlobs = 20.0F;
    return cv::SimpleBlobDetector::create(params);
}

bool findGrid(const cv::Mat& mono8, std::vector<cv::Point2f>& centers) {
    return cv::findCirclesGrid(
        mono8, cv::Size(10, 10), centers,
        cv::CALIB_CB_SYMMETRIC_GRID | cv::CALIB_CB_CLUSTERING,
        createDarkCircleDetector());
}

double neighborSpacingStandardDeviation(const std::vector<cv::Point2f>& centers) {
    std::vector<double> distances;
    distances.reserve(180);
    for (int row = 0; row < 10; ++row) {
        for (int column = 0; column < 10; ++column) {
            const int index = row * 10 + column;
            if (column + 1 < 10) {
                distances.push_back(cv::norm(centers[index + 1] - centers[index]));
            }
            if (row + 1 < 10) {
                distances.push_back(cv::norm(centers[index + 10] - centers[index]));
            }
        }
    }
    const double mean = std::accumulate(distances.begin(), distances.end(), 0.0) / distances.size();
    double squared_sum = 0.0;
    for (double distance : distances) squared_sum += (distance - mean) * (distance - mean);
    return std::sqrt(squared_sum / distances.size());
}

bool hasColorOverlay(const cv::Mat& bgr) {
    for (int row = 0; row < bgr.rows; ++row) {
        const auto* pixels = bgr.ptr<cv::Vec3b>(row);
        for (int column = 0; column < bgr.cols; ++column) {
            if (pixels[column][0] != pixels[column][1] || pixels[column][1] != pixels[column][2]) {
                return true;
            }
        }
    }
    return false;
}

cv::Mat createRadiallyDistortedGrid() {
    cv::Mat image(400, 400, CV_8UC1, cv::Scalar(230));
    const cv::Point2d image_center(200.0, 200.0);
    for (int row = 0; row < 10; ++row) {
        for (int column = 0; column < 10; ++column) {
            const cv::Point2d ideal(47.0 + column * 34.0, 47.0 + row * 34.0);
            const cv::Point2d delta = ideal - image_center;
            const double normalized_radius2 = delta.dot(delta) / (200.0 * 200.0);
            const cv::Point2d observed = image_center + delta * (1.0 + 0.04 * normalized_radius2);
            cv::circle(image, observed, 7, cv::Scalar(10), cv::FILLED, cv::LINE_AA);
        }
    }
    return image;
}

} // namespace

int main() {
    const cv::Mat distorted = createRadiallyDistortedGrid();
    std::vector<cv::Point2f> before_centers;
    if (!findGrid(distorted, before_centers)) return 1;
    const double before_spacing_stddev = neighborSpacingStandardDeviation(before_centers);

    wafer_calib::DotGridDistortionTemplate cpp_template;
    cv::Mat diagnostic_bgr;
    const wafer_calib::Status build_status =
        wafer_calib::DistortionCorrectionModule::createDotGridTemplate(
            distorted, 10, 10, 0.15, cpp_template, diagnostic_bgr);
    if (!build_status.ok()) return 2;
    if (cpp_template.detected_point_count != 100) return 8;
    if (cpp_template.rms_error_pixels <= 0.0) return 9;
    if (diagnostic_bgr.type() != CV_8UC3 || !hasColorOverlay(diagnostic_bgr)) return 10;

    cv::Mat corrected_cpp;
    const wafer_calib::Status correct_status =
        wafer_calib::DistortionCorrectionModule::correctByDotGridTemplate(
            distorted, cpp_template, corrected_cpp);
    std::vector<cv::Point2f> after_centers;
    if (!correct_status.ok() || !findGrid(corrected_cpp, after_centers)) return 3;
    const double after_spacing_stddev = neighborSpacingStandardDeviation(after_centers);
    if (after_spacing_stddev >= before_spacing_stddev * 0.60) return 4;

    WaferDotGridDistortionTemplate c_template{};
    std::vector<unsigned char> c_diagnostic_buffer(static_cast<size_t>(distorted.total()) * 3U);
    const int c_build_status = Wafer_CreateDotGridDistortionTemplate(
        distorted.data, distorted.cols, distorted.rows, 10, 10, 0.15,
        &c_template, c_diagnostic_buffer.data());
    cv::Mat c_diagnostic(distorted.rows, distorted.cols, CV_8UC3, c_diagnostic_buffer.data());
    if (c_build_status != WAFER_SUCCESS || !hasColorOverlay(c_diagnostic)) return 5;

    std::vector<unsigned char> c_corrected_buffer(distorted.total());
    const int c_correct_status = Wafer_CorrectImageByDotGridTemplate(
        distorted.data, distorted.cols, distorted.rows, &c_template, c_corrected_buffer.data());
    cv::Mat c_corrected(distorted.rows, distorted.cols, CV_8UC1, c_corrected_buffer.data());
    if (c_correct_status != WAFER_SUCCESS || cv::countNonZero(c_corrected < 100) < 100) return 6;

    // A fixed-view grid may contain a missing dot and an off-grid dark contaminant.
    cv::Mat imperfect_grid = distorted.clone();
    cv::circle(imperfect_grid, {323, 359}, 10, cv::Scalar(230), cv::FILLED, cv::LINE_AA);
    cv::circle(imperfect_grid, {280, 25}, 7, cv::Scalar(10), cv::FILLED, cv::LINE_AA);
    wafer_calib::DotGridDistortionTemplate imperfect_template;
    cv::Mat imperfect_diagnostic;
    const wafer_calib::Status imperfect_status =
        wafer_calib::DistortionCorrectionModule::createDotGridTemplate(
            imperfect_grid, 10, 10, 0.15, imperfect_template, imperfect_diagnostic);
    if (!imperfect_status.ok() || imperfect_template.detected_point_count != 99 ||
        !hasColorOverlay(imperfect_diagnostic)) {
        return 7;
    }
    return 0;
}
