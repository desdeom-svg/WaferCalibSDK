# WaferCalibSDK

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![OpenCV](https://img.shields.io/badge/OpenCV-4.x-green.svg)](https://opencv.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey.svg)]()
[![License](https://img.shields.io/badge/License-Proprietary-red.svg)]()

`WaferCalibSDK` 是专为半导体晶圆检测、显微光学测量与工业机器视觉设计的高性能标定与几何测量算法库。SDK 基于现代 C++17 与 OpenCV 构建，提供简洁高效的 **C ABI 动态链接库接口**，可无缝用于 C++、C# (.NET / WPF / WinForms)、LabVIEW 及 Python 等上位机测控软件中。

---

## 📑 核心功能模块

### 1. 圆中心与图像几何中心差值计算 (`CircleCenterOffset`)
- **功能特点**：
  - 针对工业晶圆低对比度（灰度差 15~30 级）、多曝光变化与高斯颗粒噪声，采用**多尺度高斯差分 (DoG)** 与**自适应动态分位数阈值**提取圆形目标；
  - 采用稳健**最小二乘代数圆拟合**，实现亚像素级精度输出（拟合半径标准差 $< 0.07$ 像素）；
  - 快速精确计算圆中心与图像几何中心（$W/2.0, H/2.0$）的偏移量 $(dx, dy)$；
  - 自动输出标注图：包含圆真实轮廓、拟合圆周、圆心十字、图像中心十字、连线与左上角数据面板；
  - 单张 2048×2048 高清图单核耗时仅需 **~32 ms**。

### 2. 四十字 Mark 中心与对角线交点定位 (`MarkCenter`)
- 自动检测四个象限中的十字 Mark，自适应不同倍率与 Mark 尺寸；
- 投影极值精确定位十字中心，求解两对角线亚像素几何交点并输出诊断标注图。

### 3. 水平标定线角度检测 (`LineAngle`)
- 高精度检测暗色标定直线，支持微弱倾角（$[-90^\circ, 90^\circ)$）的亚像素直线拟合与偏角测量。

### 4. 点阵畸变建模与几何校正 (`DistortionCorrection`)
- **单视野标定**：基于单张规则圆点阵标定图建立二维三阶多项式映射模板；
- **5 视野大画幅平移联合标定 (`MultiViewJoint`)**：
  - 针对大画幅显微物镜边缘无法覆盖的痛点，支持载物平台平移 [中心, 左上, 左下, 右上, 右下] 5 视野采图；
  - 自动定位第 101 个中心特征锚点锁定 $10 \times 10$ 物理网格拓扑，结合重叠区域自适应约束各视野间相对刚体位姿；
  - 采用 Huber 鲁棒迭代加权最小二乘求解全局唯一的二维三阶双多项式模板（实测全像面 RMS 残差达 **~0.20 px** 亚像素极致精度）；
  - 自动输出 **$12288 \times 12288$ 像素 3x3 十字物理拓扑无损拼接诊断大图**，各视野独立绘制自身原图背景与检测圆点；
  - 原生支持 `.json`（人类可读）与 `.bin`（高速紧凑）双格式标定文件读写。

### 5. 线扫相机 X 方向畸变校正 (`LineScanXOnly`)
- 针对线扫相机横向非线性畸变进行单轴高阶建模与实时硬件加速校正。

### 6. 多帧暗场背景降噪模板生成 (`NoiseReduction`)
- 支持均值法、中值法及 3-Sigma 剔除均值法合成高信噪比暗场背景模板。

---

## 🏛️ 项目结构

```text
WaferCalibSDK/
├── include/                     # 公共头文件
│   └── wafer_calib/
│       ├── core/                # 核心类型与状态码 (types.hpp, status.hpp)
│       ├── modules/             # C++ 算法模块头文件
│       │   ├── circle_center_offset.hpp
│       │   ├── mark_center.hpp
│       │   ├── line_angle.hpp
│       │   ├── distortion_correction.hpp
│       │   └── noise_reduction.hpp
│       ├── c_api/               # C ABI 导出头文件 (wafer_calib_c.h)
│       └── wafer_calib.hpp      # SDK 主头文件
├── src/                         # 源码实现
│   ├── modules/                 # C++ 各算法模块实现
│   └── c_api/                   # C ABI 包装实现 (wafer_calib_c.cpp)
├── samples/                     # 示例程序 (C++ / C API 调用示例)
├── tests/                       # 单元测试与集成测试
├── docs/                        # API 详细调用说明与技术文档
├── run_generate_48_results.bat  # 一键批量处理 48 张图脚本
├── generate_project.bat         # CMake 一键生成 Visual Studio 工程
├── CMakeLists.txt               # CMake 构建脚本
├── .gitignore                   # Git 忽略配置
└── README.md                    # 项目说明文档
```

---

## 🛠️ 构建与编译指南

### 环境要求
- **操作系统**：Windows 10 / 11 (x64)
- **编译工具**：Visual Studio 2019 / 2022 (MSVC v142/v143, 支持 C++17)
- **构建系统**：CMake $\ge 3.20$
- **依赖库**：OpenCV 4.x (如 OpenCV 4.1.0 / 4.10.0 / 4.11.0)

### 编译步骤

1. **配置 OpenCV 路径**（可在 CMakeLists.txt 中设置或通过命令行传入）：
   ```powershell
   cmake -B build -S . -DOpenCV_DIR="D:/soft/opencv410/build/x64/vc15/lib"
   ```

2. **编译 Release 版本**：
   ```powershell
   cmake --build build --config Release
   ```

3. **运行单元与自动化测试**：
   ```powershell
   .\build\Release\test_circle_center_offset.exe
   .\build\Release\test_mark_center.exe
   .\build\Release\test_line_angle.exe
   .\build\Release\test_distortion_correction.exe
   .\build\Release\test_multi_view_distortion_correction.exe
   ```

---

## 💻 快速上手代码示例

### C ABI 调用示例 (以求圆中心差值为例)

```cpp
#include "wafer_calib/c_api/wafer_calib_c.h"
#include <vector>
#include <iostream>

int main() {
    int width = 2048;
    int height = 2048;
    // image_buffer: Mono8 图像数据 (长度为 width * height)
    std::vector<unsigned char> image_buffer(width * height);
    std::vector<unsigned char> result_bgr(width * height * 3);

    double offset_x = 0.0;
    double offset_y = 0.0;

    int ret = Wafer_FindCircleCenterOffset(
        image_buffer.data(),
        width,
        height,
        &offset_x,
        &offset_y,
        result_bgr.data()
    );

    if (ret == WAFER_SUCCESS) {
        std::cout << "圆中心相对图像中心偏移: dx=" << offset_x << " px, dy=" << offset_y << " px" << std::endl;
    }
    return 0;
}
```

### C# P/Invoke 声明与调用示例

```csharp
using System;
using System.Runtime.InteropServices;

internal static class WaferCalibNative
{
    private const string DllName = "WaferCalibSDK.dll";
    public const int Success = 0;

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int Wafer_FindCircleCenterOffset(
        [In] byte[] imageBuffer,
        int width,
        int height,
        out double offsetX,
        out double offsetY,
        [Out] byte[] resultBgr);
}

// 上位机业务调用：
byte[] mono8Buffer = LoadMono8Image("sample.bmp", out int width, out int height);
byte[] resultBgr = new byte[width * height * 3];

int status = WaferCalibNative.Wafer_FindCircleCenterOffset(
    mono8Buffer, width, height, out double dx, out double dy, resultBgr);

if (status == WaferCalibNative.Success)
{
    double imgCenterX = width / 2.0;
    double imgCenterY = height / 2.0;
    double circleCenterX = imgCenterX + dx;
    double circleCenterY = imgCenterY + dy;

    Console.WriteLine($"圆心位置: ({circleCenterX:F3}, {circleCenterY:F3})");
    Console.WriteLine($"中心偏移: dx={dx:F3} px, dy={dy:F3} px");
}
```

---

## 📖 详细接口文档

详细的 C API 接口规范、数据结构、返回码及各功能模块使用说明请参考：
👉 [WaferCalibSDK C API 调用说明文档 (Markdown)](docs/WaferCalibSDK_C_API调用说明.md)

---

## 📄 授权与许可

本项目源码受知识产权保护，仅供授权用户与内部项目使用。
