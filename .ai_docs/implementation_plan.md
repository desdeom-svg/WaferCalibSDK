# 双视野大基线水平标定线全局角度检测功能实施计划

## 1. 用户需求确认与答复

1. **确定只移动 X 轴、不移动 Y 轴**：
   - **完全采纳**：在双视野大基线角度解算核心算法及 C API 接口中，正式**移除 `stage_delta_y_mm` 参数**。
   - 接口入参简化为仅需 `stage_delta_x_mm`（机械 X 轴在两视野之间的位移物理距离，mm）与 `pixel_scale_y_um`（垂直方向像元当量，$\mu$m/px，可直接由 9 点标定接口输出获取）。

2. **输出双视野联合对齐诊断拼接大图的分辨率**：
   - **推荐工业标准看板分辨率：`3000 × 1800`**（或由入参 `diag_width, diag_height` 自定义）。
   - **设计考量**：
     - **轻量高效**：原图 2 张 4096×4096 若以 1:1 纯拼图（8192×4096），BGR 内存将超 100 MB，在工控软件 (C#/Qt) 界面渲染或写盘会有明显卡顿延迟；采用 `3000 × 1800` 占用内存仅约 16.2 MB，耗时极低（数十毫秒内生成），适配 2K/4K 显示屏。
     - **信息高密度看板布局**：
       - **顶部 HUD 状态栏 (高 150 px)**：大号醒目字体显示全局大基线直线角度 $\Theta_{\text{global}}$（包含“度”与“角分”）、状态及两端视野局部角度对比。
       - **核心左右对比视口 (双通道，高约 1350 px)**：
         - 左通道（宽约 1400 px）：展示【视野 1 (左端起点)】整图缩略图 + 直线亚像素边缘/拟合线局部特写子图，标注测量高度 $v_1$。
         - 右通道（宽约 1400 px）：展示【视野 2 (右端终点)】整图缩略图 + 直线亚像素边缘/拟合线局部特写子图，标注测量高度 $v_2$。
       - **中间对齐基准桥 (宽约 200 px)**：绘制跨越左右两视野的水平基准虚线与落差指示箭头，直观呈现像面落差 $\Delta v$ (px) 与物理落差 $\Delta Y$ (mm)。
       - **底部度量参数清单 (高 300 px)**：详细列出机械移动距 $\Delta X_{\text{stage}}$、像元当量 $s_y$、像面落差、物理计算公式展开及一致性指标。
     - **接口弹性控制**：支持传入 `diag_width, diag_height`，如传 `NULL, 0, 0` 则完全不生成图像，零内存占用、纳秒级跳过绘图。

---

## 2. 数学机理与计算公式

### 2.1 物理几何量与像面映射
- 设视野 1 图像宽度为 $W$，高度为 $H$，在图像中心坚线 $u_c = W/2$ 处测得直线的亚像素中心高度为 $v_1$；
- 设视野 2 图像在中心坚线 $u_c = W/2$ 处测得直线的亚像素中心高度为 $v_2$；
- 机械 X 轴从视野 1 移动到视野 2 的位移量为 $\Delta X_{\text{stage}} > 0$（单位：mm），机台 Y 轴锁死不变 ($\Delta Y_{\text{stage}} = 0$)；
- 像元 Y 方向物理当量为 $s_y = \text{pixel\_scale\_y\_um} / 1000.0$（单位：mm/px）。

### 2.2 全局基线角度公式
直线上对应于两视野中心测点之间的像面垂直像素差为：
$$\Delta v = v_2 - v_1 \quad (\text{像素})$$
转换为工件/物理基准上的垂直几何高度差：
$$\Delta Y_{\text{line\_world}} = -(v_2 - v_1) \times \left(\frac{\text{pixel\_scale\_y\_um}}{1000.0}\right) \quad (\text{mm})$$
*(注：图像坐标系中 $v$ 轴向下为正；若 $v_2 > v_1$ 说明右侧直线在图像中向下偏移，物理上工件右侧向下倾斜，故取负号)*

两测点在物理 X 方向的跨距为：
$$\Delta X_{\text{line\_world}} = \Delta X_{\text{stage}} \quad (\text{mm})$$

最终跨双视野大基线全局角度为：
$$\Theta_{\text{global}} = \arctan\left(\frac{\Delta Y_{\text{line\_world}}}{\Delta X_{\text{stage}}}\right) \times \frac{180}{\pi} \quad (\text{度})$$
角分表示：
$$\Theta_{\text{arcmin}} = \Theta_{\text{global}} \times 60 \quad (\text{角分})$$

---

## 3. C API 接口设计

在 `include/wafer_calib/c_api/wafer_calib_c.h` 中导出极简平铺接口：

```c
/**
 * @brief 跨双视野大基线高精度水平标定线全局角度检测。
 * @note 适用于相机固定，机械轴仅在 X 方向移动大跨度距离分别拍摄两端视野的场景。
 *       结合 X 轴移动物理跨距与像面亚像素垂直落差，将测角精度提高 1~2 个数量级。
 * @param mono8_view1 视野 1 (最左端) Mono8 图像指针，长度 width * height 字节。
 * @param mono8_view2 视野 2 (最右端) Mono8 图像指针，长度 width * height 字节。
 * @param width 图像宽度，单位：像素 (如 4096)。
 * @param height 图像高度，单位：像素 (如 4096)。
 * @param stage_delta_x_mm 机械 X 轴从视野 1 到视野 2 移动的物理距离，单位：毫米 (mm，如 285.000)。
 * @param pixel_scale_y_um 垂直 Y 方向像元当量，单位：微米/像素 (um/px，如 0.9009)。
 * @param out_global_angle_deg 输出大基线超高精度全局角度，单位：度 (范围 [-90, 90))。
 * @param out_view1_angle_deg 可选输出视野 1 单图局部拟合角度 (度)；传 NULL 则不获取。
 * @param out_view2_angle_deg 可选输出视野 2 单图局部拟合角度 (度)；传 NULL 则不获取。
 * @param diagnostic_bgr 可选输出双视野联合对齐诊断拼接大图缓冲区 (3通道 BGR，大小 diag_width * diag_height * 3 字节；传 NULL 表示不生成)。
 * @param diag_width 诊断大图宽度 (推荐 3000)。
 * @param diag_height 诊断大图高度 (推荐 1800)。
 * @return WAFER_SUCCESS 或 WAFER_ERR_* 错误码。
 */
WAFER_API int Wafer_FindTwoViewHorizontalLineAngle(
    const unsigned char* mono8_view1,
    const unsigned char* mono8_view2,
    int width,
    int height,
    double stage_delta_x_mm,
    double pixel_scale_y_um,
    double* out_global_angle_deg,
    double* out_view1_angle_deg,
    double* out_view2_angle_deg,
    unsigned char* diagnostic_bgr,
    int diag_width,
    int diag_height
);
```

---

## 4. 实施步骤

1. **核心算法扩展** (`include/wafer_calib/modules/line_angle.hpp` & `src/modules/line_angle.cpp`)：
   - 提取单图水平直线亚像素方程和中心纵坐标 $v_c$ 的底层辅助方法；
   - 实现 `findTwoViewHorizontalLineAngle`，处理双视野亚像素直线提取、大基线角度联立求解与 3000×1800 工业看板诊断图合成。
2. **C API 导出与封装** (`include/wafer_calib/c_api/wafer_calib_c.h` & `src/c_api/wafer_calib_c.cpp`)：
   - 导出 `Wafer_FindTwoViewHorizontalLineAngle` 接口，完成严密参数校验与异常保护。
3. **单元与集成测试** (`tests/test_two_view_line_angle.cpp`)：
   - 载入 `D:\Projects\opencvProject\WaferCalibSDK\images\根据线输出角度` 下的实测图“角度调整1.bmp”和“角度调整2.bmp”；
   - 验证轴位移与解算精度，导出 3000×1800 诊断图并落盘校验。
4. **工程构建与交付**：
   - MSBuild 编译 Release x64 DLL/LIB，同步更新至 `docs/` 目录；
   - 更新 `docs/WaferCalibSDK_C_API调用说明.md`，添加双视野直线角度检测章节与 C# P/Invoke 示例；
   - 运行 Python 脚本生成最新 `docs/WaferCalibSDK_C_API调用说明.pdf`；
   - 更新 `.ai_docs/walkthrough.md`。