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

WAFER_API int Wafer_CreateMultiViewDotGridDistortionTemplate(
    const unsigned char** image_buffers,
    int width,
    int height,
    int grid_columns,
    int grid_rows,
    double point_spacing_mm,
    double stage_step_mm,
    WaferDotGridDistortionTemplate* out_template,
    unsigned char* result_bgr
) {
    if (!image_buffers || width <= 0 || height <= 0 || grid_columns < 2 || grid_rows < 2 ||
        point_spacing_mm <= 0.0 || !out_template) {
        return WAFER_ERR_INVALID_PARAM;
    }

    const wafer_calib::CalibrationViewPosition positions[5] = {
        wafer_calib::CalibrationViewPosition::Center,
        wafer_calib::CalibrationViewPosition::TopLeft,
        wafer_calib::CalibrationViewPosition::BottomLeft,
        wafer_calib::CalibrationViewPosition::TopRight,
        wafer_calib::CalibrationViewPosition::BottomRight
    };

    std::vector<wafer_calib::CalibrationViewInput> views;
    views.reserve(5);
    for (int i = 0; i < 5; ++i) {
        if (!image_buffers[i]) {
            return WAFER_ERR_INVALID_PARAM;
        }
        wafer_calib::CalibrationViewInput view;
        view.position = positions[i];
        view.image_mono8 = cv::Mat(height, width, CV_8UC1, const_cast<unsigned char*>(image_buffers[i]));
        views.push_back(view);
    }

    wafer_calib::DotGridDistortionTemplate cpp_template;
    cv::Mat diagnostic;
    const wafer_calib::Status status = wafer_calib::DistortionCorrectionModule::createMultiViewDotGridTemplate(
        views, grid_columns, grid_rows, point_spacing_mm, stage_step_mm, cpp_template, diagnostic);

    if (!status.ok()) {
        return statusToCErrorCode(status);
    }

    dotGridTemplateToC(cpp_template, *out_template);

    if (result_bgr && !diagnostic.empty()) {
        const int diag_w = width * 3;
        const int diag_h = height * 3;
        cv::Mat out_diag(diag_h, diag_w, CV_8UC3, result_bgr);
        if (diagnostic.cols == diag_w && diagnostic.rows == diag_h) {
            diagnostic.copyTo(out_diag);
        } else {
            cv::resize(diagnostic, out_diag, cv::Size(diag_w, diag_h), 0, 0, cv::INTER_AREA);
        }
    }

    return WAFER_SUCCESS;
}

WAFER_API int Wafer_CreateMultiViewDotGridTemplateFromFiles(
    const char** file_paths,
    int grid_columns,
    int grid_rows,
    double point_spacing_mm,
    double stage_step_mm,
    WaferDotGridDistortionTemplate* out_template,
    const char* save_diagnostic_image_path
) {
    if (!file_paths || grid_columns < 2 || grid_rows < 2 || point_spacing_mm <= 0.0) {
        return WAFER_ERR_INVALID_PARAM;
    }

    const wafer_calib::CalibrationViewPosition positions[5] = {
        wafer_calib::CalibrationViewPosition::Center,
        wafer_calib::CalibrationViewPosition::TopLeft,
        wafer_calib::CalibrationViewPosition::BottomLeft,
        wafer_calib::CalibrationViewPosition::TopRight,
        wafer_calib::CalibrationViewPosition::BottomRight
    };

    std::vector<wafer_calib::CalibrationViewInput> views;
    views.reserve(5);
    int width = 0;
    int height = 0;

    for (int i = 0; i < 5; ++i) {
        if (!file_paths[i]) {
            return WAFER_ERR_INVALID_PARAM;
        }
        cv::Mat img = wafer_calib::readImageUnicode(file_paths[i], cv::IMREAD_GRAYSCALE);
        if (img.empty()) {
            return WAFER_ERR_IMAGE_EMPTY;
        }
        if (width == 0) {
            width = img.cols;
            height = img.rows;
        } else if (img.cols != width || img.rows != height) {
            return WAFER_ERR_FORMAT_MISMATCH;
        }

        wafer_calib::CalibrationViewInput view;
        view.position = positions[i];
        view.image_mono8 = img;
        views.push_back(view);
    }

    wafer_calib::DotGridDistortionTemplate cpp_template;
    cv::Mat diagnostic;
    const wafer_calib::Status status = wafer_calib::DistortionCorrectionModule::createMultiViewDotGridTemplate(
        views, grid_columns, grid_rows, point_spacing_mm, stage_step_mm, cpp_template, diagnostic);

    if (!status.ok()) {
        return statusToCErrorCode(status);
    }

    if (out_template) {
        dotGridTemplateToC(cpp_template, *out_template);
    }

    if (save_diagnostic_image_path && save_diagnostic_image_path[0] != '\0' && !diagnostic.empty()) {
        wafer_calib::writeImageUnicode(save_diagnostic_image_path, diagnostic);
    }

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

WAFER_API int Wafer_SaveDistortionTemplateToFile(
    const char* file_path,
    const WaferDotGridDistortionTemplate* distortion_template
) {
    if (!file_path || file_path[0] == '\0' || !distortion_template) {
        return WAFER_ERR_INVALID_PARAM;
    }

    const wafer_calib::DotGridDistortionTemplate cpp_template = dotGridTemplateFromC(*distortion_template);
    const wafer_calib::Status status = wafer_calib::DistortionCorrectionModule::saveTemplateToFile(file_path, cpp_template);
    return statusToCErrorCode(status);
}

WAFER_API int Wafer_LoadDistortionTemplateFromFile(
    const char* file_path,
    WaferDotGridDistortionTemplate* distortion_template
) {
    if (!file_path || file_path[0] == '\0' || !distortion_template) {
        return WAFER_ERR_INVALID_PARAM;
    }

    wafer_calib::DotGridDistortionTemplate cpp_template{};
    const wafer_calib::Status status = wafer_calib::DistortionCorrectionModule::loadTemplateFromFile(file_path, cpp_template);
    if (!status.ok()) {
        return statusToCErrorCode(status);
    }

    dotGridTemplateToC(cpp_template, *distortion_template);
    return WAFER_SUCCESS;
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

WAFER_API int Wafer_CalculatePlatformRotationCenter(
    const unsigned char** image_buffers,
    int image_count,
    int width,
    int height,
    WaferPlatformRotationCenterResult* out_result,
    unsigned char* result_bgr
) {
    if (!image_buffers || image_count < 3 || width <= 0 || height <= 0 || !out_result) {
        return WAFER_ERR_INVALID_PARAM;
    }

    std::vector<cv::Mat> images;
    images.reserve(image_count);
    for (int i = 0; i < image_count; ++i) {
        if (!image_buffers[i]) {
            return WAFER_ERR_INVALID_PARAM;
        }
        images.emplace_back(height, width, CV_8UC1, const_cast<unsigned char*>(image_buffers[i]));
    }

    wafer_calib::PlatformRotationCenterResult cpp_result;
    cv::Mat diagnostic;
    const wafer_calib::Status status = wafer_calib::PlatformRotationCenterModule::calculateRotationCenter(
        images, cpp_result, diagnostic);

    if (!status.ok()) {
        return statusToCErrorCode(status);
    }

    out_result->center_x = cpp_result.rotation_center.x;
    out_result->center_y = cpp_result.rotation_center.y;
    out_result->radius = cpp_result.rotation_radius;
    out_result->rms_error = cpp_result.rms_error;
    out_result->max_error = cpp_result.max_error;
    out_result->valid_point_count = std::min(cpp_result.valid_point_count, WAFER_ROTATION_CENTER_MAX_POINTS);

    for (int i = 0; i < out_result->valid_point_count; ++i) {
        out_result->point_x[i] = cpp_result.detected_points[i].x;
        out_result->point_y[i] = cpp_result.detected_points[i].y;
        out_result->point_residual[i] = cpp_result.radial_residuals[i];
    }

    if (result_bgr && !diagnostic.empty()) {
        cv::Mat out_mat(height, width, CV_8UC3, result_bgr);
        diagnostic.copyTo(out_mat);
    }

    return WAFER_SUCCESS;
}

WAFER_API int Wafer_CalculatePlatformRotationCenterFromFiles(
    const char** file_paths,
    int file_count,
    WaferPlatformRotationCenterResult* out_result,
    const char* save_diagnostic_image_path
) {
    if (!file_paths || file_count < 3) {
        return WAFER_ERR_INVALID_PARAM;
    }

    std::vector<cv::Mat> images;
    images.reserve(file_count);
    for (int i = 0; i < file_count; ++i) {
        if (!file_paths[i]) {
            return WAFER_ERR_INVALID_PARAM;
        }
        cv::Mat img = wafer_calib::readImageUnicode(file_paths[i], cv::IMREAD_GRAYSCALE);
        if (img.empty()) {
            return WAFER_ERR_IMAGE_EMPTY;
        }
        images.push_back(img);
    }

    wafer_calib::PlatformRotationCenterResult cpp_result;
    cv::Mat diagnostic;
    const wafer_calib::Status status = wafer_calib::PlatformRotationCenterModule::calculateRotationCenter(
        images, cpp_result, diagnostic);

    if (!status.ok()) {
        return statusToCErrorCode(status);
    }

    if (out_result) {
        out_result->center_x = cpp_result.rotation_center.x;
        out_result->center_y = cpp_result.rotation_center.y;
        out_result->radius = cpp_result.rotation_radius;
        out_result->rms_error = cpp_result.rms_error;
        out_result->max_error = cpp_result.max_error;
        out_result->valid_point_count = std::min(cpp_result.valid_point_count, WAFER_ROTATION_CENTER_MAX_POINTS);

        for (int i = 0; i < out_result->valid_point_count; ++i) {
            out_result->point_x[i] = cpp_result.detected_points[i].x;
            out_result->point_y[i] = cpp_result.detected_points[i].y;
            out_result->point_residual[i] = cpp_result.radial_residuals[i];
        }
    }

    if (save_diagnostic_image_path && save_diagnostic_image_path[0] != '\0' && !diagnostic.empty()) {
        if (!wafer_calib::writeImageUnicode(save_diagnostic_image_path, diagnostic)) {
            return WAFER_ERR_FILE_IO;
        }
    }

    return WAFER_SUCCESS;
}

int Wafer_CalibrateReflectanceLut(
    const unsigned char** image_buffers,
    int width,
    int height,
    const WaferReflectanceLutConfig* config,
    WaferReflectanceLutResult* out_result,
    unsigned char* result_bgr
) {
    if (!image_buffers || width <= 0 || height <= 0 || !out_result) {
        return WAFER_ERR_INVALID_PARAM;
    }

    std::vector<cv::Mat> images;
    images.reserve(4);
    for (int i = 0; i < 4; ++i) {
        if (!image_buffers[i]) {
            return WAFER_ERR_IMAGE_EMPTY;
        }
        images.push_back(cv::Mat(height, width, CV_8UC1, const_cast<unsigned char*>(image_buffers[i])));
    }

    wafer_calib::ReflectanceLutConfig cpp_cfg;
    if (config) {
        cpp_cfg.mode = (config->lut_mode == 1) ? wafer_calib::WAFER_LUT_MODE_SMOOTH : wafer_calib::WAFER_LUT_MODE_DEADBAND;
        cpp_cfg.roi_center_width = config->roi_center_width;
        cpp_cfg.roi_center_height = config->roi_center_height;
        cpp_cfg.deadband_width = config->deadband_width;
        for (int k = 0; k < 4; ++k) {
            if (config->target_values[k] > 0.0) {
                cpp_cfg.target_values[k] = config->target_values[k];
            }
        }
    }

    wafer_calib::ReflectanceLutResult cpp_res;
    cv::Mat diagnostic;
    cv::Mat* diag_ptr = result_bgr ? &diagnostic : nullptr;

    if (!wafer_calib::WaferReflectanceLutCalibrator::Calibrate(images, cpp_cfg, cpp_res, diag_ptr)) {
        return WAFER_ERR_FITTING_FAILED;
    }

    // 拷贝结果
    std::memcpy(out_result->lut, cpp_res.lut, 256);
    for (int k = 0; k < 4; ++k) {
        out_result->measured_peaks[k] = cpp_res.measured_peaks[k];
        out_result->target_values[k] = cpp_res.target_values[k];
    }
    out_result->raw_linearity_r2 = cpp_res.raw_linearity_r2;
    out_result->corrected_linearity_r2 = cpp_res.corrected_linearity_r2;

    if (result_bgr && !diagnostic.empty()) {
        const int diag_w = 2048;
        const int diag_h = 1536;
        if (diagnostic.cols == diag_w && diagnostic.rows == diag_h) {
            std::memcpy(result_bgr, diagnostic.data, diag_w * diag_h * 3);
        } else {
            cv::Mat resized;
            cv::resize(diagnostic, resized, cv::Size(diag_w, diag_h));
            std::memcpy(result_bgr, resized.data, diag_w * diag_h * 3);
        }
    }

    return WAFER_SUCCESS;
}

int Wafer_CalibrateReflectanceLutFromFiles(
    const char** file_paths,
    const WaferReflectanceLutConfig* config,
    WaferReflectanceLutResult* out_result,
    const char* save_diagnostic_image_path
) {
    if (!file_paths) {
        return WAFER_ERR_INVALID_PARAM;
    }

    std::vector<cv::Mat> images;
    images.reserve(4);
    for (int i = 0; i < 4; ++i) {
        if (!file_paths[i]) {
            return WAFER_ERR_INVALID_PARAM;
        }
        cv::Mat img = wafer_calib::readImageUnicode(file_paths[i], cv::IMREAD_GRAYSCALE);
        if (img.empty()) {
            return WAFER_ERR_FILE_IO;
        }
        images.push_back(img);
    }

    wafer_calib::ReflectanceLutConfig cpp_cfg;
    if (config) {
        cpp_cfg.mode = (config->lut_mode == 1) ? wafer_calib::WAFER_LUT_MODE_SMOOTH : wafer_calib::WAFER_LUT_MODE_DEADBAND;
        cpp_cfg.roi_center_width = config->roi_center_width;
        cpp_cfg.roi_center_height = config->roi_center_height;
        cpp_cfg.deadband_width = config->deadband_width;
        for (int k = 0; k < 4; ++k) {
            if (config->target_values[k] > 0.0) {
                cpp_cfg.target_values[k] = config->target_values[k];
            }
        }
    }

    wafer_calib::ReflectanceLutResult cpp_res;
    cv::Mat diagnostic;
    cv::Mat* diag_ptr = (save_diagnostic_image_path && save_diagnostic_image_path[0] != '\0') ? &diagnostic : nullptr;

    if (!wafer_calib::WaferReflectanceLutCalibrator::Calibrate(images, cpp_cfg, cpp_res, diag_ptr)) {
        return WAFER_ERR_FITTING_FAILED;
    }

    if (out_result) {
        std::memcpy(out_result->lut, cpp_res.lut, 256);
        for (int k = 0; k < 4; ++k) {
            out_result->measured_peaks[k] = cpp_res.measured_peaks[k];
            out_result->target_values[k] = cpp_res.target_values[k];
        }
        out_result->raw_linearity_r2 = cpp_res.raw_linearity_r2;
        out_result->corrected_linearity_r2 = cpp_res.corrected_linearity_r2;
    }

    if (save_diagnostic_image_path && save_diagnostic_image_path[0] != '\0' && !diagnostic.empty()) {
        if (!wafer_calib::writeImageUnicode(save_diagnostic_image_path, diagnostic)) {
            return WAFER_ERR_FILE_IO;
        }
    }

    return WAFER_SUCCESS;
}

int Wafer_ApplyLutToImage(
    const unsigned char* src_mono8,
    int width,
    int height,
    const unsigned char* lut_256,
    unsigned char* dst_mono8
) {
    if (!src_mono8 || !lut_256 || !dst_mono8 || width <= 0 || height <= 0) {
        return WAFER_ERR_INVALID_PARAM;
    }

    cv::Mat src(height, width, CV_8UC1, const_cast<unsigned char*>(src_mono8));
    cv::Mat dst(height, width, CV_8UC1, dst_mono8);

    if (!wafer_calib::WaferReflectanceLutCalibrator::ApplyLut(src, dst, lut_256)) {
        return WAFER_ERR_UNKNOWN;
    }

    return WAFER_SUCCESS;
}

int Wafer_SaveLutToFile(
    const char* file_path,
    const unsigned char* lut_256
) {
    if (!file_path || !lut_256) {
        return WAFER_ERR_INVALID_PARAM;
    }
    if (!wafer_calib::WaferReflectanceLutCalibrator::SaveLut(file_path, lut_256)) {
        return WAFER_ERR_FILE_IO;
    }
    return WAFER_SUCCESS;
}

int Wafer_LoadLutFromFile(
    const char* file_path,
    unsigned char* lut_256
) {
    if (!file_path || !lut_256) {
        return WAFER_ERR_INVALID_PARAM;
    }
    if (!wafer_calib::WaferReflectanceLutCalibrator::LoadLut(file_path, lut_256)) {
        return WAFER_ERR_FILE_IO;
    }
    return WAFER_SUCCESS;
}

} // extern "C"

