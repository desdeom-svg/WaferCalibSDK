#ifndef WAFER_CALIB_CORE_STATUS_HPP_
#define WAFER_CALIB_CORE_STATUS_HPP_

#include <string>

namespace wafer_calib {

// SDK 统一错误状态码
enum class ErrorCode {
    Success = 0,               // 操作成功
    InvalidParam = 1,          // 无效参数（如空图像、超出范围参数）
    ImageEmpty = 2,            // 输入图像为空
    ImageFormatMismatch = 3,   // 图像格式或尺寸不匹配
    FeatureNotFound = 4,       // 未能提取到足够特征（点阵、Mark点或直线）
    FittingFailed = 5,         // 数据拟合或方程求解失败
    FileIOError = 6,           // 文件读写错误
    UnknownError = 999         // 未知错误
};

// 状态返回结构体
struct Status {
    ErrorCode code = ErrorCode::Success;
    std::string message;

    Status() = default;
    Status(ErrorCode err_code, const std::string& msg = "") : code(err_code), message(msg) {}

    bool ok() const {
        return code == ErrorCode::Success;
    }

    static Status OK() {
        return Status(ErrorCode::Success, "OK");
    }

    static Status Error(ErrorCode err_code, const std::string& msg) {
        return Status(err_code, msg);
    }
};

} // namespace wafer_calib

#endif // WAFER_CALIB_CORE_STATUS_HPP_
