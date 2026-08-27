#include <array>
#include <vector>

#include "wafer_calib/c_api/wafer_calib_c.h"
#include "wafer_calib/modules/noise_reduction.hpp"

int verifyTemplate(
    const unsigned char** frames,
    int method,
    const std::vector<unsigned char>& expected
) {
    std::array<unsigned char, 4> output = {};
    const int result = Wafer_CreateDarkFrameTemplate(
        frames, 3, output.data(), 2, 2, method);

    if (result != WAFER_SUCCESS) {
        return 1;
    }
    return std::vector<unsigned char>(output.begin(), output.end()) == expected ? 0 : 2;
}

int main() {
    const unsigned char frame0[] = {1, 100, 3, 4};
    const unsigned char frame1[] = {2, 20, 4, 8};
    const unsigned char frame2[] = {3, 30, 5, 6};
    const unsigned char* frames[] = {frame0, frame1, frame2};
    if (verifyTemplate(frames, 0 /* Mean */, {2, 50, 4, 6}) != 0) {
        return 1;
    }
    if (verifyTemplate(frames, 1 /* Median */, {2, 30, 4, 6}) != 0) {
        return 2;
    }
    if (verifyTemplate(frames, 2 /* SigmaClip */, {2, 50, 4, 6}) != 0) {
        return 3;
    }

    const cv::Mat mono16_frame(1, 1, CV_16UC1, cv::Scalar(42));
    cv::Mat mono16_output;
    const wafer_calib::Status mono16_status = wafer_calib::NoiseReductionModule::createDarkFrameTemplate(
        {mono16_frame}, mono16_output, wafer_calib::FrameCombineMethod::Median);
    if (mono16_status.code != wafer_calib::ErrorCode::ImageFormatMismatch) {
        return 4;
    }
    return 0;
}
