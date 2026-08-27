#ifndef WAFER_CALIB_MODULES_LINE_ANGLE_HPP_
#define WAFER_CALIB_MODULES_LINE_ANGLE_HPP_

#include "wafer_calib/core/status.hpp"
#include "wafer_calib/core/types.hpp"

namespace wafer_calib {

class LineAngleModule {
public:
    static Status findHorizontalLineAngle(
        const cv::Mat& mono8,
        double& angle_degrees,
        cv::Mat& result_bgr);
};

} // namespace wafer_calib

#endif // WAFER_CALIB_MODULES_LINE_ANGLE_HPP_
