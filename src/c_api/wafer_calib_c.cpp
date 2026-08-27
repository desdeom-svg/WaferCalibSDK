#include "wafer_calib/c_api/wafer_calib_c.h"
#include "wafer_calib/wafer_calib.hpp"
#include <algorithm>
#include <vector>

static int statusToCErrorCode(const wafer_calib::Status& status) {
    if (status.ok()) return WAFER_SUCCESS;
    switch (status.code) {
        case wafer_calib::ErrorCode::InvalidParam: return WAFER_ERR_INVALID_PARAM;
        case wafer_calib::ErrorCode::ImageEmpty: return WAFER_ERR_IMAGE_EMPTY;
        case wafer_calib::ErrorCode::ImageFormatMismatch: return WAFER_ERR_FORMAT_MISMATCH;
        case wafer_calib::ErrorCode::FeatureNotFound: return WAFER_ERR_FEATURE_NOT_FOUND;
        case wafer_calib::ErrorCode::FittingFailed: return WAFER_ERR_FITTING_FAILED;
        case wafer_calib::ErrorCode::FileIOError: return WAFER_ERR_FILE_IO;
        default: return WAFER_ERR_UNKNOWN;
    }
}

static wafer_calib::DotGridDistortionTemplate dotGridTemplateFromC(
    const WaferDotGridDistortionTemplate& c_template
) {
    wafer_calib::DotGridDistortionTemplate cpp_template;
    cpp_template.image_width = c_template.image_width;
    cpp_template.image_height = c_template.image_height;
    cpp_template.detected_point_count = c_template.detected_point_count;
    std::copy(
        c_template.output_to_input_x,
        c_template.output_to_input_x + WAFER_DOT_GRID_POLYNOMIAL_COEFFICIENT_COUNT,
        cpp_template.output_to_input_x.begin());
    std::copy(
        c_template.output_to_input_y,
        c_template.output_to_input_y + WAFER_DOT_GRID_POLYNOMIAL_COEFFICIENT_COUNT,
        cpp_template.output_to_input_y.begin());
    cpp_template.rms_error_pixels = c_template.rms_error_pixels;
    return cpp_template;
}

static void dotGridTemplateToC(
    const wafer_calib::DotGridDistortionTemplate& cpp_template,
    WaferDotGridDistortionTemplate& c_template
) {
    c_template.image_width = cpp_template.image_width;
    c_template.image_height = cpp_template.image_height;
    c_template.detected_point_count = cpp_template.detected_point_count;
    std::copy(
        cpp_template.output_to_input_x.begin(),
        cpp_template.output_to_input_x.end(),
        c_template.output_to_input_x);
    std::copy(
        cpp_template.output_to_input_y.begin(),
        cpp_template.output_to_input_y.end(),
        c_template.output_to_input_y);
    c_template.rms_error_pixels = cpp_template.rms_error_pixels;
}

extern "C" {

WAFER_API int Wafer_FindHorizontalLineAngle(
    const unsigned char* image_buffer,
    int width,
    int height,
    double* angle_degrees,
    unsigned char* result_bgr
) {
    if (!image_buffer || width <= 0 || height <= 0 || !angle_degrees || !result_bgr) {
        return WAFER_ERR_INVALID_PARAM;
    }

    cv::Mat input(height, width, CV_8UC1, const_cast<unsigned char*>(image_buffer));
    cv::Mat output(height, width, CV_8UC3, result_bgr);
    const wafer_calib::Status status = wafer_calib::LineAngleModule::findHorizontalLineAngle(
        input, *angle_degrees, output);
    return statusToCErrorCode(status);
}

WAFER_API int Wafer_CreateDotGridDistortionTemplate(
    const unsigned char* calibration_image_buffer,
    int width,
    int height,
    int grid_columns,
    int grid_rows,
    double point_spacing_mm,
    WaferDotGridDistortionTemplate* out_template,
    unsigned char* result_bgr
) {
    if (!calibration_image_buffer || width <= 0 || height <= 0 || grid_columns < 2 || grid_rows < 2 ||
        point_spacing_mm <= 0.0 || !out_template || !result_bgr) {
        return WAFER_ERR_INVALID_PARAM;
    }

    cv::Mat input(height, width, CV_8UC1, const_cast<unsigned char*>(calibration_image_buffer));
    cv::Mat diagnostic(height, width, CV_8UC3, result_bgr);
    wafer_calib::DotGridDistortionTemplate cpp_template;
    const wafer_calib::Status status = wafer_calib::DistortionCorrectionModule::createDotGridTemplate(
        input, grid_columns, grid_rows, point_spacing_mm, cpp_template, diagnostic);
    if (!status.ok()) {
        return statusToCErrorCode(status);
    }

    dotGridTemplateToC(cpp_template, *out_template);
    return WAFER_SUCCESS;
}

WAFER_API int Wafer_CorrectImageByDotGridTemplate(
    const unsigned char* image_buffer,
    int width,
    int height,
    const WaferDotGridDistortionTemplate* distortion_template,
    unsigned char* corrected_mono8
) {
    if (!image_buffer || width <= 0 || height <= 0 || !distortion_template || !corrected_mono8) {
        return WAFER_ERR_INVALID_PARAM;
    }

    cv::Mat input(height, width, CV_8UC1, const_cast<unsigned char*>(image_buffer));
    cv::Mat corrected(height, width, CV_8UC1, corrected_mono8);
    const wafer_calib::DotGridDistortionTemplate cpp_template = dotGridTemplateFromC(*distortion_template);
    const wafer_calib::Status status = wafer_calib::DistortionCorrectionModule::correctByDotGridTemplate(
        input, cpp_template, corrected);
    return statusToCErrorCode(status);
}

WAFER_API int Wafer_FindFourCrossMarkCenter(
    const unsigned char* image_buffer,
    int width,
    int height,
    double* calculated_center_x,
    double* calculated_center_y,
    unsigned char* result_bgr
) {
    if (!image_buffer || width <= 0 || height <= 0 || !calculated_center_x ||
        !calculated_center_y || !result_bgr) {
        return WAFER_ERR_INVALID_PARAM;
    }

    cv::Mat input(height, width, CV_8UC1, const_cast<unsigned char*>(image_buffer));
    cv::Mat output(height, width, CV_8UC3, result_bgr);
    wafer_calib::Point2D calculated_center;
    const wafer_calib::Status status = wafer_calib::MarkCenterModule::findFourCrossMarkCenter(
        input, calculated_center, output);
    if (!status.ok()) {
        return statusToCErrorCode(status);
    }

    *calculated_center_x = calculated_center.x;
    *calculated_center_y = calculated_center.y;
    return WAFER_SUCCESS;
}

WAFER_API int Wafer_CreateDarkFrameTemplate(
    const unsigned char** frame_buffers,
    int frame_count,
    unsigned char* out_dark_template,
    int width,
    int height,
    int method
) {
    if (!frame_buffers || frame_count <= 0 || !out_dark_template || width <= 0 || height <= 0) {
        return WAFER_ERR_INVALID_PARAM;
    }

    std::vector<cv::Mat> frames;
    frames.reserve(frame_count);

    for (int i = 0; i < frame_count; ++i) {
        if (!frame_buffers[i]) return WAFER_ERR_INVALID_PARAM;
        cv::Mat frame(height, width, CV_8UC1, const_cast<unsigned char*>(frame_buffers[i]));
        frames.push_back(frame);
    }

    cv::Mat out_mat(height, width, CV_8UC1, out_dark_template);
    wafer_calib::FrameCombineMethod combine_method = static_cast<wafer_calib::FrameCombineMethod>(method);

    wafer_calib::Status status = wafer_calib::NoiseReductionModule::createDarkFrameTemplate(frames, out_mat, combine_method);
    return statusToCErrorCode(status);
}

WAFER_API int Wafer_CreateDarkFrameTemplateFromFiles(
    const char** file_paths,
    int file_count,
    const char* save_output_path,
    int method
) {
    if (!file_paths || file_count <= 0) {
        return WAFER_ERR_INVALID_PARAM;
    }

    std::vector<cv::Mat> frames;
    frames.reserve(file_count);

    for (int i = 0; i < file_count; ++i) {
        if (!file_paths[i]) continue;
        cv::Mat img = wafer_calib::readImageUnicode(file_paths[i], cv::IMREAD_GRAYSCALE);
        if (!img.empty()) {
            frames.push_back(img);
        }
    }

    if (frames.empty()) {
        return WAFER_ERR_IMAGE_EMPTY;
    }

    cv::Mat out_dark_template;
    wafer_calib::FrameCombineMethod combine_method = static_cast<wafer_calib::FrameCombineMethod>(method);
    wafer_calib::Status status = wafer_calib::NoiseReductionModule::createDarkFrameTemplate(frames, out_dark_template, combine_method);

    if (status.ok() && save_output_path && save_output_path[0] != '\0') {
        std::vector<uchar> buf;
        cv::imencode(".bmp", out_dark_template, buf);
#ifdef _WIN32
        std::wstring wsave = wafer_calib::stringToWstring(save_output_path);
        std::ofstream file(wsave, std::ios::binary);
#else
        std::ofstream file(save_output_path, std::ios::binary);
#endif
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        }
    }

    return statusToCErrorCode(status);
}

WAFER_API int Wafer_FindCircleCenterOffset(
    const unsigned char* image_buffer,
    int width,
    int height,
    double* offset_x,
    double* offset_y,
    unsigned char* result_bgr
) {
    if (!image_buffer || width <= 0 || height <= 0 || !offset_x || !offset_y || !result_bgr) {
        return WAFER_ERR_INVALID_PARAM;
    }

    cv::Mat input(height, width, CV_8UC1, const_cast<unsigned char*>(image_buffer));
    cv::Mat diagnostic(height, width, CV_8UC3, result_bgr);

    wafer_calib::CircleDetectionResult detection_result;
    const wafer_calib::Status status = wafer_calib::CircleCenterOffsetModule::findCircleCenterOffset(
        input, detection_result, diagnostic);

    if (!status.ok()) {
        return statusToCErrorCode(status);
    }

    *offset_x = detection_result.offset_x;
    *offset_y = detection_result.offset_y;

    return WAFER_SUCCESS;
}

} // extern "C"
