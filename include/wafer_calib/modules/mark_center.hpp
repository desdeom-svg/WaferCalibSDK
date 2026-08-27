#ifndef WAFER_CALIB_MODULES_MARK_CENTER_HPP_
#define WAFER_CALIB_MODULES_MARK_CENTER_HPP_

#include "wafer_calib/core/status.hpp"
#include "wafer_calib/core/types.hpp"

namespace wafer_calib {

class MarkCenterModule {
public:
    static Status findFourCrossMarkCenter(
        const cv::Mat& mono8,
        Point2D& calculated_center,
        cv::Mat& result_bgr);
};

} // namespace wafer_calib

#endif // WAFER_CALIB_MODULES_MARK_CENTER_HPP_
