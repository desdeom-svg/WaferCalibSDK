cmake_minimum_required(VERSION 3.20)
project(WaferCalibSDK VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# 设置 OpenCV 查找路径
if(NOT DEFINED OpenCV_DIR)
    if(DEFINED ENV{OPENCV_DIR})
        set(OpenCV_DIR "$ENV{OPENCV_DIR}/x64/vc16/lib")
    else()
        set(OpenCV_DIR "D:/DevSDKs/opencv4100/opencv/build/x64/vc16/lib")
    endif()
endif()

find_package(OpenCV REQUIRED)

message(STATUS "OpenCV found Version: ${OpenCV_VERSION}")
message(STATUS "OpenCV Include Dir: ${OpenCV_INCLUDE_DIRS}")

# 头文件包含路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

# 搜集 SDK 源码
file(GLOB_RECURSE SDK_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# 构建 SDK 共享库/静态库
add_library(WaferCalibSDK SHARED ${SDK_SOURCES})
target_compile_definitions(WaferCalibSDK PRIVATE WAFER_CALIB_EXPORTS)
target_link_libraries(WaferCalibSDK PRIVATE ${OpenCV_LIBS})

# 编译 samples 示例程序
file(GLOB SAMPLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/samples/*.cpp")
foreach(SAMPLE_SRC ${SAMPLE_SOURCES})
    get_filename_component(SAMPLE_NAME ${SAMPLE_SRC} NAME_WE)
    add_executable(${SAMPLE_NAME} ${SAMPLE_SRC})
    target_link_libraries(${SAMPLE_NAME} PRIVATE WaferCalibSDK ${OpenCV_LIBS})
endforeach()

add_executable(test_c_api_packed_mono8 tests/test_c_api_packed_mono8.cpp)
target_link_libraries(test_c_api_packed_mono8 PRIVATE WaferCalibSDK ${OpenCV_LIBS})

add_executable(test_mark_center tests/test_mark_center.cpp)
target_link_libraries(test_mark_center PRIVATE WaferCalibSDK ${OpenCV_LIBS})

add_executable(test_line_angle tests/test_line_angle.cpp)
target_link_libraries(test_line_angle PRIVATE WaferCalibSDK ${OpenCV_LIBS})

add_executable(test_distortion_correction tests/test_distortion_correction.cpp)
target_link_libraries(test_distortion_correction PRIVATE WaferCalibSDK ${OpenCV_LIBS})

add_executable(test_line_scan_color_distortion tests/test_line_scan_color_distortion.cpp)
target_link_libraries(test_line_scan_color_distortion PRIVATE WaferCalibSDK ${OpenCV_LIBS})

add_executable(test_circle_center_offset tests/test_circle_center_offset.cpp)
target_link_libraries(test_circle_center_offset PRIVATE WaferCalibSDK ${OpenCV_LIBS})

add_executable(circle_center_offset_tool tools/circle_center_offset_tool.cpp)
target_link_libraries(circle_center_offset_tool PRIVATE WaferCalibSDK ${OpenCV_LIBS})


