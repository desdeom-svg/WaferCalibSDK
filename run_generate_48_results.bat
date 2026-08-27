@echo off
chcp 65001 >nul
title 圆中心与图像中心差值计算 - 一键批量处理 48 张图

echo ================================================================
echo       WaferCalibSDK - 一键生成 48 张图的圆中心差值结果标注图
echo ================================================================
echo.

set "EXE_PATH=%~dp0build\Release\sample_circle_center_offset.exe"

if not exist "%EXE_PATH%" (
    echo [提示] 正在编译工程 Release 版本...
    "D:\soft\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake/bin/cmake.exe" --build "%~dp0build" --config Release
    echo.
)

if not exist "%EXE_PATH%" (
    echo [错误] 未找到可执行程序 %EXE_PATH%，请先检查编译环境。
    pause
    exit /b 1
)

echo [运行] 启动批量处理程序...
echo.
"%EXE_PATH%"
echo.
echo ================================================================
echo 批量处理完成！结果图保存在:
echo images\求圆中心与图像中心的差值\20260822_0\results\
echo ================================================================
echo.
pause
