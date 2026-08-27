@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "NO_PAUSE="
if /I "%~1"=="--no-pause" set "NO_PAUSE=1"

rem Creates output\LineScanXOnlyTool. Copy the complete folder to another PC.
for %%I in ("%~dp0..") do set "SDK_ROOT=%%~fI"
set "SOURCE_DIR=%SDK_ROOT%\build\Release"
set "OUTPUT_DIR=%SDK_ROOT%\output\LineScanXOnlyTool"
set "OPENCV_DLL=D:\soft\opencv410\build\x64\vc15\bin\opencv_world410.dll"
set "VC_RUNTIME_DIR=D:\soft\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.40.33807\x64\Microsoft.VC143.CRT"
set "VC_REDIST=D:\soft\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.40.33807\vc_redist.x64.exe"

for %%F in (
    "%SOURCE_DIR%\sample_line_scan_color_distortion.exe"
    "%SOURCE_DIR%\sample_line_scan_apply_analysis.exe"
    "%SOURCE_DIR%\WaferCalibSDK.dll"
    "%OPENCV_DLL%"
    "%VC_RUNTIME_DIR%\msvcp140.dll"
    "%VC_RUNTIME_DIR%\vcruntime140.dll"
    "%VC_RUNTIME_DIR%\vcruntime140_1.dll"
) do (
    if not exist %%F (
        set "MISSING_FILE=%%~F"
        goto :missing_file
    )
)

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
copy /y "%SOURCE_DIR%\sample_line_scan_color_distortion.exe" "%OUTPUT_DIR%\" >nul
copy /y "%SOURCE_DIR%\sample_line_scan_apply_analysis.exe" "%OUTPUT_DIR%\" >nul
copy /y "%SOURCE_DIR%\WaferCalibSDK.dll" "%OUTPUT_DIR%\" >nul
copy /y "%OPENCV_DLL%" "%OUTPUT_DIR%\" >nul
copy /y "%VC_RUNTIME_DIR%\msvcp140.dll" "%OUTPUT_DIR%\" >nul
copy /y "%VC_RUNTIME_DIR%\vcruntime140.dll" "%OUTPUT_DIR%\" >nul
copy /y "%VC_RUNTIME_DIR%\vcruntime140_1.dll" "%OUTPUT_DIR%\" >nul
if exist "%VC_REDIST%" copy /y "%VC_REDIST%" "%OUTPUT_DIR%\" >nul
copy /y "%SDK_ROOT%\tools\run_line_scan_x_only.bat" "%OUTPUT_DIR%\" >nul

echo.
echo Portable package created:
echo %OUTPUT_DIR%
echo Copy the entire LineScanXOnlyTool folder to the target PC.
echo Double-click run_line_scan_x_only.bat to start.
if not defined NO_PAUSE pause
goto :eof

:missing_file
echo Required runtime file was not found:
echo %MISSING_FILE%
echo Build Release first. Verify the configured OpenCV and VC runtime paths.
if not defined NO_PAUSE pause
exit /b 1
