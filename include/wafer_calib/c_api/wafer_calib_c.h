#ifndef WAFER_CALIB_C_API_H_
#define WAFER_CALIB_C_API_H_

/**
 * @file wafer_calib_c.h
 * @brief WaferCalibSDK 的 C ABI；可直接用于 C、C++ 及 C# P/Invoke。
 *
 * 除另行说明外，所有图像缓冲区均为紧凑存储的 Mono8：
 * 每行步长固定为 width，缓冲区长度为 width * height 字节。
 */

#ifdef _WIN32
  #ifdef WAFER_CALIB_EXPORTS
    /** @brief DLL 内部编译时导出符号。 */
    #define WAFER_API __declspec(dllexport)
  #else
    /** @brief DLL 使用方导入符号。 */
    #define WAFER_API __declspec(dllimport)
  #endif
#else
  /** @brief 非 Windows 平台的默认符号可见性。 */
  #define WAFER_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 调用成功。 */
#define WAFER_SUCCESS 0
/** @brief 空指针、尺寸、方法编号或其他参数无效。 */
#define WAFER_ERR_INVALID_PARAM 1
/** @brief 输入图像为空或无法读取。 */
#define WAFER_ERR_IMAGE_EMPTY 2
/** @brief 图像格式或图像尺寸不符合接口要求。 */
#define WAFER_ERR_FORMAT_MISMATCH 3
/** @brief 未找到所需的 Mark、直线、圆点阵等特征。 */
#define WAFER_ERR_FEATURE_NOT_FOUND 4
/** @brief 几何、直线或多项式拟合失败。 */
#define WAFER_ERR_FITTING_FAILED 5
/** @brief 文件读写失败。 */
#define WAFER_ERR_FILE_IO 6
/** @brief 未分类的内部错误。 */
#define WAFER_ERR_UNKNOWN 999

/** @brief 三阶二维多项式中每个输出坐标轴的系数数量。 */
#define WAFER_DOT_GRID_POLYNOMIAL_COEFFICIENT_COUNT 10

/**
 * @brief 单张规则点阵建立的固定视野畸变校正模板。
 *
 * 该结构体不含指针，可由 C# 按顺序布局直接保存和恢复。
 * 模板仅适用于 image_width × image_height 的 Mono8 图像。
 */
typedef struct WaferDotGridDistortionTemplate {
    /** @brief 建立模板时的图像宽度，单位：像素。 */
    int image_width;
    /** @brief 建立模板时的图像高度，单位：像素。 */
    int image_height;
    /**
     * @brief 从校正后输出坐标映射到原始输入 X 坐标的 10 个三阶多项式系数。
     * @details 系数项顺序：1、x、y、x2、xy、y2、x3、x2y、xy2、y3；x/y 为归一化输出坐标。
     */
    double output_to_input_x[WAFER_DOT_GRID_POLYNOMIAL_COEFFICIENT_COUNT];
    /**
     * @brief 从校正后输出坐标映射到原始输入 Y 坐标的 10 个三阶多项式系数。
     * @details 系数项顺序与 output_to_input_x 相同。
     */
    double output_to_input_y[WAFER_DOT_GRID_POLYNOMIAL_COEFFICIENT_COUNT];
    /** @brief 实际参与映射拟合的有效圆点数量。 */
    int detected_point_count;
    /** @brief 有效圆点的映射 RMS 残差，单位：像素。 */
    double rms_error_pixels;
} WaferDotGridDistortionTemplate;

/**
 * @brief 将 N 帧暗场 Mono8 图像合成为暗场背景噪声模板。
 * @param frame_buffers 包含 frame_count 个图像指针的数组；每帧长度为 width * height 字节。
 * @param frame_count 输入暗场图像帧数，必须大于 0。
 * @param out_dark_template 输出暗场模板缓冲区，由调用方分配 width * height 字节。
 * @param width 图像宽度，单位：像素。
 * @param height 图像高度，单位：像素。
 * @param method 合成方法：0=均值，1=中值，2=3 Sigma 剔除均值。
 * @return WAFER_SUCCESS 或 WAFER_ERR_* 错误码。
 */
WAFER_API int Wafer_CreateDarkFrameTemplate(
    const unsigned char** frame_buffers,
    int frame_count,
    unsigned char* out_dark_template,
    int width,
    int height,
    int method
);

/**
 * @brief 在一张 Mono8 图像中检测四个十字 Mark，并计算两条对角线的交点。
 * @param image_buffer 输入 Mono8 图像，长度为 width * height 字节。
 * @param width 图像宽度，单位：像素。
 * @param height 图像高度，单位：像素。
 * @param calculated_center_x 输出四个 Mark 对角线交点的 X 坐标。
 * @param calculated_center_y 输出四个 Mark 对角线交点的 Y 坐标。
 * @param result_bgr 输出 BGR 标注图缓冲区，由调用方分配 width * height * 3 字节。
 * @return WAFER_SUCCESS 或 WAFER_ERR_* 错误码。
 */
WAFER_API int Wafer_FindFourCrossMarkCenter(
    const unsigned char* image_buffer,
    int width,
    int height,
    double* calculated_center_x,
    double* calculated_center_y,
    unsigned char* result_bgr
);

/**
 * @brief 检测 Mono8 图像中黑色水平标定线的角度。
 * @param image_buffer 输入 Mono8 图像，长度为 width * height 字节。
 * @param width 图像宽度，单位：像素。
 * @param height 图像高度，单位：像素。
 * @param angle_degrees 输出角度，范围 [-90, 90)；从左向右向下倾斜为正。
 * @param result_bgr 输出 BGR 标注图缓冲区，由调用方分配 width * height * 3 字节。
 * @return WAFER_SUCCESS 或 WAFER_ERR_* 错误码。
 */
WAFER_API int Wafer_FindHorizontalLineAngle(
    const unsigned char* image_buffer,
    int width,
    int height,
    double* angle_degrees,
    unsigned char* result_bgr
);

/**
 * @brief 从一张规则圆点阵 Mono8 图建立固定视野畸变校正模板。
 * @param calibration_image_buffer 输入标定点阵 Mono8 图像，长度为 width * height 字节。
 * @param width 标定图宽度，单位：像素。
 * @param height 标定图高度，单位：像素。
 * @param grid_columns 期望点阵列数，必须不小于 2。
 * @param grid_rows 期望点阵行数，必须不小于 2。
 * @param point_spacing_mm 相邻圆点的物理间距，单位：mm，必须大于 0。
 * @param out_template 输出可保存、可复用的畸变校正模板。
 * @param result_bgr 输出 BGR 诊断图缓冲区，含圆心编号、理想网格与残差，长度为 width * height * 3 字节。
 * @return WAFER_SUCCESS 或 WAFER_ERR_* 错误码。
 */
WAFER_API int Wafer_CreateDotGridDistortionTemplate(
    const unsigned char* calibration_image_buffer,
    int width,
    int height,
    int grid_columns,
    int grid_rows,
    double point_spacing_mm,
    WaferDotGridDistortionTemplate* out_template,
    unsigned char* result_bgr
);

/**
 * @brief 使用同尺寸点阵畸变模板校正一张 Mono8 图像。
 * @param image_buffer 输入 Mono8 图像，长度为 width * height 字节。
 * @param width 输入图宽度，必须等于 distortion_template->image_width。
 * @param height 输入图高度，必须等于 distortion_template->image_height。
 * @param distortion_template 由 Wafer_CreateDotGridDistortionTemplate 得到或恢复的模板。
 * @param corrected_mono8 输出校正后的 Mono8 图像缓冲区，由调用方分配 width * height 字节。
 * @return WAFER_SUCCESS 或 WAFER_ERR_* 错误码。
 */
WAFER_API int Wafer_CorrectImageByDotGridTemplate(
    const unsigned char* image_buffer,
    int width,
    int height,
    const WaferDotGridDistortionTemplate* distortion_template,
    unsigned char* corrected_mono8
);

/**
 * @brief 从文件路径读取 N 张暗场图，合成暗场模板，并可选保存模板图。
 * @param file_paths 包含 file_count 个图像文件路径的数组；支持 Windows Unicode 中文路径。
 * @param file_count 文件路径数量，必须大于 0。
 * @param save_output_path 模板保存路径；传 NULL 或空字符串时不保存文件。
 * @param method 合成方法：0=均值，1=中值，2=3 Sigma 剔除均值。
 * @return WAFER_SUCCESS 或 WAFER_ERR_* 错误码。
 */
WAFER_API int Wafer_CreateDarkFrameTemplateFromFiles(
    const char** file_paths,
    int file_count,
    const char* save_output_path,
    int method
);

/**
 * @brief 在 Mono8 图像中检测圆形目标，计算圆中心与图像几何中心的差值 (dx, dy)，并生成诊断标注图。
 * @param image_buffer 输入 Mono8 图像缓冲区，长度为 width * height 字节。
 * @param width 图像宽度，单位：像素。
 * @param height 图像高度，单位：像素。
 * @param offset_x 输出圆中心与图像中心的 X 差值 (circle_center_x - image_center_x)，单位：像素。
 * @param offset_y 输出圆中心与图像中心的 Y 差值 (circle_center_y - image_center_y)，单位：像素。
 * @param result_bgr 输出 BGR 诊断标注图缓冲区，由调用方分配 width * height * 3 字节。
 * @return WAFER_SUCCESS 或 WAFER_ERR_* 错误码。
 */
WAFER_API int Wafer_FindCircleCenterOffset(
    const unsigned char* image_buffer,
    int width,
    int height,
    double* offset_x,
    double* offset_y,
    unsigned char* result_bgr
);

#ifdef __cplusplus
}
#endif

#endif // WAFER_CALIB_C_API_H_
