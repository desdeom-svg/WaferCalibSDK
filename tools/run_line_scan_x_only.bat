@echo off
setlocal EnableExtensions DisableDelayedExpansion
chcp 65001 >nul

rem Portable deployment: prefer EXEs beside this BAT.
set "SCRIPT_DIR=%~dp0"
set "CALIBRATION_EXE=%SCRIPT_DIR%sample_line_scan_color_distortion.exe"
set "ANALYSIS_EXE=%SCRIPT_DIR%sample_line_scan_apply_analysis.exe"

rem Development-tree fallback: <SDK root>\tools\this BAT.
if not exist "%CALIBRATION_EXE%" goto :development_tree
goto :runtime_ready

:development_tree
for %%I in ("%~dp0..") do set "SDK_ROOT=%%~fI"
set "CALIBRATION_EXE=%SDK_ROOT%\build\Release\sample_line_scan_color_distortion.exe"
set "ANALYSIS_EXE=%SDK_ROOT%\build\Release\sample_line_scan_apply_analysis.exe"

:runtime_ready
for %%I in ("%CALIBRATION_EXE%") do set "RUNTIME_DIR=%%~dpI"
set "PATH=%RUNTIME_DIR%;%PATH%"

if not exist "%CALIBRATION_EXE%" goto :missing_executable
if not exist "%ANALYSIS_EXE%" goto :missing_executable

:menu
cls
echo ================================================================
echo  WaferCalibSDK - Line-scan BGR X-only distortion correction
echo ================================================================
echo [1] Offline calibration and validation
echo [2] Apply existing JSON and validate
echo [3] Exit
echo.
set "ACTION="
set /p "ACTION=Enter 1, 2, or 3: "
if "%ACTION%"=="1" goto :calibrate
if "%ACTION%"=="2" goto :apply
if "%ACTION%"=="3" goto :eof
echo Invalid input.
pause
goto :menu

:calibrate
cls
echo --- Offline calibration and validation ---
set "CALIBRATION_IMAGE="
set /p "CALIBRATION_IMAGE=Calibration BMP path: "
set "CALIBRATION_IMAGE=%CALIBRATION_IMAGE:"=%"
if not defined CALIBRATION_IMAGE goto :missing_input
if not exist "%CALIBRATION_IMAGE%" goto :missing_input

for %%I in ("%CALIBRATION_IMAGE%") do set "IMAGE_DIR=%%~dpI"
set "RECIPE_JSON=%IMAGE_DIR%recipe_x_only.json"
echo Default recipe JSON: %RECIPE_JSON%
set /p "RECIPE_JSON=Recipe JSON output path, blank keeps default: "
set "RECIPE_JSON=%RECIPE_JSON:"=%"
if not defined RECIPE_JSON set "RECIPE_JSON=%IMAGE_DIR%recipe_x_only.json"

set "VERIFY_IMAGE=%IMAGE_DIR%output_calibration_x_only_metrics.bmp"
echo Default validation image: %VERIFY_IMAGE%
set /p "VERIFY_IMAGE=Validation BMP output path, blank keeps default: "
set "VERIFY_IMAGE=%VERIFY_IMAGE:"=%"
if not defined VERIFY_IMAGE set "VERIFY_IMAGE=%IMAGE_DIR%output_calibration_x_only_metrics.bmp"

echo.
echo [1/2] Creating X-only recipe...
call "%CALIBRATION_EXE%" --calibrate "%CALIBRATION_IMAGE%" "%RECIPE_JSON%"
if errorlevel 1 goto :command_failed

echo.
echo [2/2] Applying recipe to calibration image and printing metrics...
call "%ANALYSIS_EXE%" "%CALIBRATION_IMAGE%" "%RECIPE_JSON%" "%VERIFY_IMAGE%"
set "RESULT_CODE=%ERRORLEVEL%"
goto :report_result

:apply
cls
echo --- Apply existing recipe and validate ---
set "INPUT_IMAGE="
set /p "INPUT_IMAGE=Input BMP path: "
set "INPUT_IMAGE=%INPUT_IMAGE:"=%"
if not defined INPUT_IMAGE goto :missing_input
if not exist "%INPUT_IMAGE%" goto :missing_input

set "RECIPE_JSON="
set /p "RECIPE_JSON=X-only recipe JSON path: "
set "RECIPE_JSON=%RECIPE_JSON:"=%"
if not defined RECIPE_JSON goto :missing_input
if not exist "%RECIPE_JSON%" goto :missing_input

for %%I in ("%INPUT_IMAGE%") do set "IMAGE_DIR=%%~dpI"
set "VERIFY_IMAGE=%IMAGE_DIR%output_x_only_corrected.bmp"
echo Default corrected image: %VERIFY_IMAGE%
set /p "VERIFY_IMAGE=Corrected BMP output path, blank keeps default: "
set "VERIFY_IMAGE=%VERIFY_IMAGE:"=%"
if not defined VERIFY_IMAGE set "VERIFY_IMAGE=%IMAGE_DIR%output_x_only_corrected.bmp"

echo.
echo Applying recipe and printing before/after metrics...
call "%ANALYSIS_EXE%" "%INPUT_IMAGE%" "%RECIPE_JSON%" "%VERIFY_IMAGE%"
set "RESULT_CODE=%ERRORLEVEL%"
goto :report_result

:report_result
echo.
if "%RESULT_CODE%"=="0" (
    echo Done. Result image and metrics were generated.
) else if "%RESULT_CODE%"=="5" (
    echo Result image was generated, but grid geometry validation did not pass.
) else (
    echo Execution failed. Error code: %RESULT_CODE%.
)
pause
goto :menu

:missing_executable
echo Release sample EXE was not found beside this BAT.
echo Use tools\package_line_scan_x_only.bat to create a portable folder.
pause
goto :eof

:missing_input
echo Input path is empty or does not exist.
pause
goto :menu

:command_failed
echo Calibration failed. Validation was not run.
pause
goto :menu
