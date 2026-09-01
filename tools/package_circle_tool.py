import os
import shutil

sdk_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
source_dir = os.path.join(sdk_root, "build", "Release")
output_dir = os.path.join(sdk_root, "output", "CircleCenterOffsetTool")
opencv_dll = r"D:\soft\opencv410\build\x64\vc15\bin\opencv_world410.dll"
vc_runtime_dir = r"D:\soft\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.40.33807\x64\Microsoft.VC143.CRT"
vc_redist = r"D:\soft\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.40.33807\vc_redist.x64.exe"

print("================================================================")
print("     WaferCalibSDK - 圆中心差值离线便携运行包打包工具           ")
print("================================================================")

os.makedirs(output_dir, exist_ok=True)

# 需要拷贝的文件列表
files_to_copy = [
    os.path.join(source_dir, "circle_center_offset_tool.exe"),
    os.path.join(source_dir, "WaferCalibSDK.dll"),
    opencv_dll,
    os.path.join(vc_runtime_dir, "msvcp140.dll"),
    os.path.join(vc_runtime_dir, "vcruntime140.dll"),
    os.path.join(vc_runtime_dir, "vcruntime140_1.dll")
]

if os.path.exists(vc_redist):
    files_to_copy.append(vc_redist)

for src in files_to_copy:
    if not os.path.exists(src):
        print(f"[错误] 未找到文件: {src}")
        exit(1)
    dst = os.path.join(output_dir, os.path.basename(src))
    shutil.copy2(src, dst)
    print(f"[拷贝] {os.path.basename(src)} -> {output_dir}")

# 生成一键双击运行脚本
bat_content = """@echo off
chcp 65001 >nul
title 圆中心与图像中心差值计算 - 离线批量处理

echo ================================================================
echo       WaferCalibSDK - 圆中心与图像中心差值计算离线工具
echo ================================================================
echo.
echo [运行] 正在搜索同级目录及所有子文件夹中的所有 .bmp 图像...
echo.
"%~dp0circle_center_offset_tool.exe"
echo.
echo ================================================================
echo 处理完成！PNG 结果图保存在同级目录下的 result 文件夹中。
echo ================================================================
echo.
pause
"""

bat_path = os.path.join(output_dir, "一键运行_求圆中心差值.bat")
with open(bat_path, "w", encoding="utf-8") as f:
    f.write(bat_content)
print(f"[生成] 一键运行_求圆中心差值.bat")

# 生成说明文档
readme_content = """================================================================
          圆中心与图像中心差值计算 - 离线工具使用说明
================================================================

【功能介绍】
本工具为独立离线便携包，无需安装 Visual Studio 或 OpenCV 即可在任意
Windows 64位电脑上直接双击运行。

【使用方法】
1. 将需要分析的 .bmp 图像复制到本工具所在文件夹（即与 exe 同级目录）
   或放入任意子文件夹中；
2. 双击运行 "一键运行_求圆中心差值.bat"；
3. 程序将自动递归搜索同级目录及所有子文件夹下的所有 .bmp 图像；
4. 在同级目录下自动创建 "result" 文件夹，并将标注结果图以 .png 格式保存。

【输出说明】
- 图像中心：坐标为 (W/2.0, H/2.0)
- 圆中心：RANSAC 稳健亚像素拟合圆心坐标 (Cx, Cy)
- 差值：dx = Cx - W/2.0，dy = Cy - H/2.0
- 标注图：标记图像中心十字、圆中心十字、拟合圆周、真实轮廓与中心连线。
================================================================
"""

readme_path = os.path.join(output_dir, "使用说明.txt")
with open(readme_path, "w", encoding="utf-8") as f:
    f.write(readme_content)
print(f"[生成] 使用说明.txt")

print("\n打包成功完成！输出目录:")
print(output_dir)
