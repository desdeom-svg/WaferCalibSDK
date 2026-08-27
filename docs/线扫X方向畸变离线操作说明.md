# 线扫彩色图像仅 X 方向畸变校正：离线操作说明

## 前提

先在 SDK 根目录完成 Release 构建，使下列文件存在：

```text
build\Release\sample_line_scan_color_distortion.exe
build\Release\sample_line_scan_apply_analysis.exe
```

双击运行：

```text
tools\run_line_scan_x_only.bat
```

BAT 会自动定位 SDK 根目录；路径可直接粘贴，带或不带双引号均可。

BAT 的交互提示使用英文以避免 Windows CMD 的中文编码差异；菜单 `1/2/3` 与下述中文步骤一一对应。

## 可复制部署到其他电脑

不要只复制 `run_line_scan_x_only.bat`。在构建电脑上双击：

```text
tools\package_line_scan_x_only.bat
```

它会生成：

```text
output\LineScanXOnlyTool\
```

该目录包含启动 BAT、两个样例 EXE、`WaferCalibSDK.dll`、`opencv_world410.dll` 与应用本地 VC 运行库。复制整个 `LineScanXOnlyTool` 文件夹到目标电脑后，双击其中的 `run_line_scan_x_only.bat` 即可；不要改变其中 EXE、DLL 和 BAT 的相对位置。

附带的 `vc_redist.x64.exe` 可在目标电脑缺少 Microsoft VC++ 运行库时手工安装。

## 1. 离线标定并验证

在菜单选择 `1`，依次输入：

1. 黄色圆点阵标定图 BMP 路径；
2. 配方 JSON 的输出路径；直接回车默认在标定图目录生成 `recipe_x_only.json`；
3. 验证结果 BMP 的输出路径；直接回车默认生成 `output_calibration_x_only_metrics.bmp`。

输出内容：

- `recipe_x_only.json`：仅 X 方向二次畸变校正配方；
- `output_line_scan_detection.bmp`：圆点检测与拟合诊断图；
- `output_line_scan_corrected.bmp`：标定图矫正结果；
- 指定的验证结果图；
- 控制台指标：检测圆心数、有效列数、行斜率、行倾角、横向点距标准差、倾角保持和几何验证结论。

`行倾角` 应基本保持不变；`横向点距标准差` 应下降。

## 2. 应用已有配方并验证

在菜单选择 `2`，依次输入：

1. 待矫正 BGR BMP 图像路径；
2. 标定阶段生成的 X-only JSON 路径；
3. 结果 BMP 的输出路径；直接回车默认在输入图目录生成 `output_x_only_corrected.bmp`。

BAT 调用分析样例完成实际矫正，并在控制台输出校正前后的同一组指标。

## 注意事项

- 输入必须是 BGR8 BMP，且宽度必须与配方中的标定宽度一致；图像高度允许不同。
- 只有含可检测黄色点阵的图像才能输出完整几何指标。普通业务图仍可输出矫正图，但会提示未完成点阵几何验证。
- 配方为 `line_scan_x_quadratic_only_v1`，只矫正 X 方向；不会旋转、剪切或改变图像的 Y 倾斜。
- 每套相机、镜头、倍率和扫描方向建议使用独立目录保存标定图、JSON 和结果图，避免覆盖固定名称的诊断结果。
