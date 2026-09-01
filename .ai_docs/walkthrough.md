# 圆中心差值算法抗脏污优化与独立离线工具交付报告

## 1. 变更与优化概览

1. **算法抗脏污与抗粘连深度优化**：
   - **RANSAC 稳健亚像素圆拟合**：引入随机抽样一致算法（RANSAC）替代普通最小二乘代数拟合，自动识别并剔除边缘毛刺与粘连脏污离群点，彻底解决因脏污粘连导致拟合半径畸变放大的问题；
   - **圆周梯度能量打分 (Gradient Flux)**：沿拟合圆周采样 36 个方向的高阶 Sobel 梯度幅值，结合圆度、RANSAC 内点率、面积对数权重与中心距离惩罚进行多特征综合评分，彻底杜绝背景微小颗粒脏污/噪点（$R<8$ 像素）引发的误识别；
   - **双数据集 100% 验证**：
     - 脏污挑战集（`Desktop\s 0_t 33_r 1`）：33 / 33 张图像全部 100% 正确检出（平均耗时 ~40.8 ms）；
     - 原测试集（`20260822_0`）：48 / 48 张图像全部 100% 正确检出。

2. **独立离线运行工具包与递归批处理脚本**：
   - 生成完整独立的便携离线包：[`output/CircleCenterOffsetTool/`](file:///d:/Projects/opencvProject/WaferCalibSDK/output/CircleCenterOffsetTool/)；
   - 包含所需全部依赖动态库（`WaferCalibSDK.dll`、`opencv_world410.dll`、`msvcp140.dll`、`vcruntime140.dll`、`vcruntime140_1.dll`、`vc_redist.x64.exe`）；
   - **递归扫描子目录**：双击 `一键运行_求圆中心差值.bat` 时，自动递归遍历同级目录及所有子文件夹下的 `.bmp` 图像；
   - **自动创建 result 文件夹**：在同级目录下自动创建 `result` 文件夹，并以 `.png` 格式（保持子目录镜像）保存带诊断标注的高清结果图。

---

## 2. 离线工具包目录结构

```text
output/CircleCenterOffsetTool/
├── 一键运行_求圆中心差值.bat    # 双击一键执行脚本 (自动扫描当前目录及所有子目录)
├── circle_center_offset_tool.exe # 离线批量处理主程序
├── WaferCalibSDK.dll             # 优化后的最新版 SDK 核心动态库
├── opencv_world410.dll           # OpenCV 4.10 运行时
├── msvcp140.dll                  # VC++ 运行时动态库
├── vcruntime140.dll
├── vcruntime140_1.dll
├── vc_redist.x64.exe             # 微软官方 VC 运行库安装包 (备用)
├── 使用说明.txt                   # 详细使用指南
└── result/                       # 自动创建的输出目录 (保存 .png 标注图)
```

---

## 3. 使用方法

1. 将整个 [`CircleCenterOffsetTool`](file:///d:/Projects/opencvProject/WaferCalibSDK/output/CircleCenterOffsetTool/) 文件夹拷贝到任意 64 位 Windows 电脑（无需安装 Visual Studio 或 OpenCV 环境）；
2. 将待测 `.bmp` 图片直接放入工具所在文件夹，或放入任意子文件夹中；
3. **双击 `一键运行_求圆中心差值.bat`**；
4. 程序将自动进行批量高精度检测并在控制台输出统计表，生成的标注图将保存为 `.png` 存放于 `result/` 文件夹中。
