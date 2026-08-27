@echo off
chcp 65001 >nul
echo 正在为 WaferCalibSDK 生成 Visual Studio 解决方案...

set "CMAKE_EXE=D:\soft\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not exist "%CMAKE_EXE%" (
    set "CMAKE_EXE=cmake"
)

"%CMAKE_EXE%" -B "%~dp0build" -S "%~dp0." -G "Visual Studio 17 2022" -A x64

if %ERRORLEVEL% equ 0 (
    echo.
    echo ========================================================
    echo  VS 解决方案生成成功！
    echo  解决方案路径: %~dp0build\WaferCalibSDK.sln
    echo ========================================================
) else (
    echo.
    echo [错误] 生成失败，请检查 CMake 配置。
)
pause