# 水平标定线角度检测算法全面重构与鲁棒性优化方案

## 1. 问题背景与失效根因剖析

用户反馈 `C:\Users\HYC\Desktop\CameraAjustCalibration` 目录下的 3 张相机调节标定图（`Mark_1.bmp`, `Mark_2.bmp`, `Mark_3.bmp`，4096×4096 Mono8）无法被 `Wafer_FindHorizontalLineAngle` 识别（错误码 `ErrorCode::FeatureNotFound`，Code = 4）。

经过深入数据探查与机理分析，定位到旧算法失效的 4 大核心根因：
1. **硬编码单一极性假定**：旧算法采用 `cv::threshold(mono8, foreground, 0, 255, THRESH_BINARY_INV | THRESH_OTSU)`，强制假定标定线必须是“高亮背景下的暗线”。而实际现场中存在高亮条带（`Mark_1/2` 灰度 191，背景 165）与纯白瞄准十字线（`Mark_3` 灰度 255），反向二值化时这些特征直接被置零消除。
2. **全局直方图缺乏空间与方向信息**：4096×4096 大图拥有 1677 万像素，标定线面积占比极小（<0.5%）。全图 Otsu 阈值完全被背景渐晕、大面积光照不均所主导，导致弱对比度线和刻槽无法分割。
3. **缺乏方向性形态滤波**：原算法采用无方向性的 `3x3` 形态学核，无法滤除非水平杂斑，也无法在水平方向将局部断续的微弱划线/刻痕长程缝合。
4. **僵化的边界与长宽比过滤**：原算法对连通域的过滤条件单一，无法处理条带包含双边缘或十字线贯穿视野边界的工况。

---

## 2. 算法架构重构设计

新算法基于**方向性高通滤波 + 长程方向聚合 + 密集骨架拟合 + 伴随边缘对称中轴合成**体系构建，流程如下：

```
输入图像 (CV_8UC1)
       │
       ▼
[1] 垂直一维自适应背景估计与高通滤波 (cv::boxFilter 1xK)
       │ -> 计算局部垂直差分绝对值 |I - I_bg|，消除纵向暗角与全图背景漂移
       ▼
[2] 动态信噪比自适应阈值分割
       │ -> 基于局部垂直噪声标准差的自适应下限分割，同时响应弱刻痕 (10 DN) 与高动态亮线 (255 DN)
       ▼
[3] 水平长程形态学闭运算 (kw x 1)
       │ -> 沿水平近水平方向长程连接断裂刻痕与划线，消除垂直噪声
       ▼
[4] 连通域筛选与密集亚像素骨架拟合
       │ -> 过滤尺寸过短（< w/4）及竖向杂斑（w < 2h）
       │ -> 沿 X 轴密集提取骨架质心，使用 cv::fitLine 拟合精确倾角与直线度残差 RMSE
       ▼
[5] 垂直物理分层聚类与伴随边缘对称中轴合成
       │ -> 垂直间距 < 250px 且角度差 < 0.8° 的平行候选归为同一物理结构
       │ -> 若检测到宽条带的成对伴随边缘，采用对称中轴合成精确高度 y_center = (y1 + y2)/2
       ▼
[6] 主标定线系综合评分决策与加权角度融合
       │ -> 基于有效宽度、对比度能量、纵横比与直线度残差综合打分，锁定唯一主标定线
       ▼
输出：精确倾角 (angle_degrees)、测点亚像素中心 (center_y)、诊断图 (result_bgr)
```

---

## 3. 拟修改代码清单

- [`src/modules/line_angle.cpp`](file:///d:/Projects/opencvProject/WaferCalibSDK/src/modules/line_angle.cpp)：
  - 核心重构 `LineAngleModule::extractLineInfo`
  - 补充候选线聚类、对称中轴计算及多模态综合评价逻辑
- [`tests/test_line_angle.cpp`](file:///d:/Projects/opencvProject/WaferCalibSDK/tests/test_line_angle.cpp)：保持既有用例兼容
- [`tests/test_two_view_line_angle.cpp`](file:///d:/Projects/opencvProject/WaferCalibSDK/tests/test_two_view_line_angle.cpp)：保持既有用例兼容
- 编写验证脚本与 C API 自动化测试，覆盖 `C:\Users\HYC\Desktop\CameraAjustCalibration` 下所有样本。

---

## 4. 验证与回归指标

1. **历史测试用例 100% 通过**：
   - `test_line_angle.exe`（单图倾角、贴边倾角、截断线抗干扰）全 PASS，误差 < 0.2°；
   - `test_two_view_line_angle.exe`（合成大基线测角与真实机台图像 `角度调整1/2.bmp`）全 PASS，大基线落差 $\Delta v \approx -1740$ px，角度 $0.315^\circ$；
2. **新增现场测试用例 100% 成功检出**：
   - `Mark_1.bmp`：返回 `WAFER_SUCCESS (0)`，稳定检出水平标定带；
   - `Mark_2.bmp`：返回 `WAFER_SUCCESS (0)`，稳定检出水平标定带；
   - `Mark_3.bmp`：返回 `WAFER_SUCCESS (0)`，稳定检出中心十字瞄准线/水平标定带；
3. **单帧性能**：4096×4096 图像在 C++ 下耗时 $\le 50$ ms。
