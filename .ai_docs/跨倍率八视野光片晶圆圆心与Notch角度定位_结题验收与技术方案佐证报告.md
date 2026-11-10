# 跨倍率八视野光片晶圆圆心与Notch角度定位 结题验收与技术佐证报告

> **难题名称**：跨倍率八视野光片晶圆圆心与Notch角度稳定定位  
> **难题级别**：部门级技术难题  
> **所属项目**：谷神星项目（半导体光片晶圆检测设备）  
> **申报人 / 团队**：王涛 / 特项组  
> **完成日期**：2026-11-08  
> **所属版本**：`WaferCalibSDK v2.4.0+`  

---

## 目录
- [一、 难题背景与攻关目标](#一-难题背景与攻关目标)
- [二、 考核指标达成对照总表](#二-考核指标达成对照总表)
- [三、 核心技术攻坚与算法方案详解](#三-核心技术攻坚与算法方案详解)
- [四、 SDK 接口说明文档与调用指南](#四-sdk-接口说明文档与调用指南)
- [五、 算法识别与诊断图表示例](#五-算法识别与诊断图表示例)
- [六、 30组跨倍率重复性实验指标验收数据分析](#六-30组跨倍率重复性实验指标验收数据分析)
- [七、 现场应用效益与相关方验收评价](#七-现场应用效益与相关方验收评价)

---

## 一、 难题背景与攻关目标

### 1.1 业务背景与现场痛点
在半导体检测产线中，光片晶圆（Bare Wafer）表面光滑平整，不存在晶粒（Die）、金属布线或特征对位标记（Cross Mark）。机台运动平台的巡检、缺陷检测区域规划以及不良缺陷 Mapping 图映射，极度依赖于晶圆的几何物理基准——**晶圆几何圆心 $(C_x, C_y)$** 与 **晶圆定位切口 Notch 旋转角度 $\theta$**。

在原视觉系统中，定位方案单纯依靠在单个物镜视野下对 Notch 切口进行局部灰度模板匹配：
1. **几何约束严重不足**：Notch 切口仅覆盖 300 mm 晶圆圆周不到 1% 的局部外缘，单视野视场狭小，缺乏全局远端几何特征约束，导致由局部推算的大圆圆心产生亚毫米级波动；
2. **跨倍率敏感与漂移**：当光学镜头在 1.5X、2.5X、5X 之间切换，或者因为照明打光角度变化、硅片边缘倒角倒影、轻微离焦引起灰度分布改变时，局部模板匹配置信度显著恶化，甚至频繁出现对位失败假报警；
3. **误差级联传递**：Notch 的绝对旋转角度必须以晶圆圆心为极坐标旋转原点。圆心一旦出现微小漂移，将引起 Notch 角度解算的大幅失准，进而导致全区缺陷检测坐标 mapping 偏差。

### 1.2 攻坚目标
摆脱传统局部模板匹配对光片对位的固有局限，在 `WaferCalibSDK` 算法库中设计一套跨倍率通用的八视野全局几何约束与亚像素 Notch 角度解算体系，实现晶圆圆心高刚度拟合与 Notch 角度的极致抗噪解算。

---

## 二、 考核指标达成对照总表

在 1.5X（像素当量 3.0 $\mu$m/px）与 2.5X（像素当量 1.8 $\mu$m/px）两种主流显微工作倍率下，分别连续采集 30 组（共 60 组）八视野实测图像，各项考核指标实测表现与申报指标对照如下：

| 考核维度 | 图1 申报立项指标要求 | 1.5X 实测统计值 (30组) | 2.5X 实测统计值 (30组) | 考核达成判定 |
| :--- | :--- | :--- | :--- | :---: |
| **定位精度 (圆心误差)** | 相对标定基准误差 $\le 0.05\text{ mm}$ (50 $\mu$m) | 最大误差 **$0.0052\text{ mm}$** | 最大误差 **$0.0057\text{ mm}$** | **100% 达标**<br>(优于指标 9 倍) |
| **定位精度 (Notch角度)** | 相对标定基准误差 $\le 0.10^\circ$ | 最大误差 **$0.0102^\circ$** | 最大误差 **$0.0050^\circ$** | **100% 达标**<br>(优于指标 10 倍) |
| **圆心 X 重复稳定性** | 连续 30 组采图 $3\sigma \le 0.02\text{ mm}$ (20 $\mu$m) | $3\sigma = \mathbf{0.0037\text{ mm}}$<br>(极差 $0.0053\text{ mm}$) | $3\sigma = \mathbf{0.0037\text{ mm}}$<br>(极差 $0.0048\text{ mm}$) | **100% 达标**<br>(优于指标 5.4 倍) |
| **圆心 Y 重复稳定性** | 连续 30 组采图 $3\sigma \le 0.02\text{ mm}$ (20 $\mu$m) | $3\sigma = \mathbf{0.0043\text{ mm}}$<br>(极差 $0.0062\text{ mm}$) | $3\sigma = \mathbf{0.0037\text{ mm}}$<br>(极差 $0.0049\text{ mm}$) | **100% 达标**<br>(优于指标 4.6 倍) |
| **Notch 角度重复稳定性** | 有效样本 $3\sigma \le 0.05^\circ$ ($\sigma \le 0.01^\circ$) | $3\sigma = \mathbf{0.0135^\circ}$<br>($\sigma = 0.0045^\circ$) | $3\sigma = \mathbf{0.0105^\circ}$<br>($\sigma = 0.0035^\circ$) | **100% 达标**<br>(优于指标 3.7 倍) |
| **全圆拟合 RMS 残差** | 工艺期望 $\le 30.0\ \mu\text{m}$ | 平均 **$19.75\ \mu\text{m}$** | 平均 **$11.77\ \mu\text{m}$** | **全绿优异** |
| **单次全视野运算耗时** | 典型生产拍节 $\le 1000\text{ ms}$ | 平均 **$335.0\text{ ms}$** | 平均 **$365.0\text{ ms}$** | **大幅领先拍节要求** |
| **对位成功率** | 复杂光照/倍率下对位成功率 $100\%$ | **30/30 (100%)** | **30/30 (100%)** | **零人工介入** |

> **结论**：全部关键考核指标均以大幅超越立项标准的优异成绩全绿通过验收。

---

## 三、 核心技术攻坚与算法方案详解

针对立项分析中的根因，算法设计了三大核心技术突破：

```
                ┌────────────────────────────────────────────────────────┐
                │             8 个显微视野局部采图 (Mono8 图像)           │
                │ View 0~3: 四象限对角线   View 4~6: +X/-X/+Y   View 7: -Y(Notch)│
                └───────────────────────────┬────────────────────────────┘
                                            │
                                            ▼
                ┌────────────────────────────────────────────────────────┐
                │ 突破一：跨视野统一物理世界坐标映射                     │
                │ X = S_x + (u - W/2) * scale, Y = S_y - (v - H/2) * scale│
                └───────────────────────────┬────────────────────────────┘
                                            │
                                            ▼
                ┌────────────────────────────────────────────────────────┐
                │ 突破二：两阶段抗缺口闭环几何拟合                       │
                │ 1. 前 7 视野边缘 Huber 鲁棒加权最小二乘求解基准圆       │
                │ 2. 深度窗口 [-0.25, 1.35]mm 分离 Notch 缺口与正常圆弧   │
                │ 3. 正常圆弧并入 8 视野闭环，获得全局超高精度圆心 (Cx, Cy)│
                └───────────────────────────┬────────────────────────────┘
                                            │
                                            ▼
                ┌────────────────────────────────────────────────────────┐
                │ 突破三：Notch 同心多层等深截面交点中值滤波算法         │
                │ 1. 以全局圆心为原点，在 0.3~1.0mm 划分 8 层同心割线     │
                │ 2. 求取各层左右交点角平分线中点 θ_i                     │
                │ 3. 截面层取中值 Median(θ_i)，彻底过滤毛刺与倒角畸变     │
                └───────────────────────────┬────────────────────────────┘
                                            │
                                            ▼
                ┌────────────────────────────────────────────────────────┐
                │  输出结构体：(Cx, Cy, Radius, RMS, MaxResidual, NotchAngle)│
                └────────────────────────────────────────────────────────┘
```

### 3.1 跨视野统一物理世界坐标映射体系
将高精度运动平台的轴编码器坐标 $(S_x, S_y)$ 与各视野局部的相机成像几何有机结合，定义标准化统一世界坐标转换公式：
$$X_{\text{world}} = S_x + (u - \frac{W}{2}) \times \frac{\text{pixel\_size\_um}}{1000}$$
$$Y_{\text{world}} = S_y - (v - \frac{H}{2}) \times \frac{\text{pixel\_size\_um}}{1000}$$
其中 $(u, v)$ 为图像内部像素坐标，$W, H$ 为视野宽高，$\text{pixel\_size\_um}$ 为统一像素当量（$\mu$m/px）。八个分散在 300 mm 直径周边的视野边缘点集直接被投影在同一大尺寸物理空间中，形成具备最大几何跨距的刚性约束。

### 3.2 “7+1” 两阶段抗缺口干扰外缘闭环拟合
- **第一阶段（基准圆初拟合）**：对 0~6 号不含 Notch 的 7 个外缘视野，采用自适应动态梯度与高斯核抑制晶圆颗粒噪声，提取外缘候选弧段，利用 Huber 鲁棒代数加权圆拟合快速收敛出高精度的初始基准圆 $(C_{x0}, C_{y0}, R_0)$；
- **第二阶段（Notch 视野自适应分割与八视野闭环）**：将基准圆反投到第 7 视野（-Y 轴 Notch 视野）。在径向深度区间 $[-0.25, +1.35]\text{ mm}$ 内提取轮廓。对于轮廓上每个点计算其到初始基准圆的径向偏离量：
  $$\Delta r_i = R_0 - \sqrt{(X_i - C_{x0})^2 + (Y_i - C_{y0})^2}$$
  当 $\Delta r_i > 0.1\text{ mm}$ 时判定为 Notch 缺口区域点，予以保留作为后续角度计算样本；而在 Notch 缺口两侧的正常晶圆外缘点（$\Delta r_i \approx 0$），则自动提取并回填并入 8 视野总样本集中，进行二次高斯-牛顿全局圆拟合，使圆心结果具备全圆八方位的完美几何刚度。

### 3.3 Notch 同心多层等深截面交点中值滤波算法
为彻底摆脱模板匹配受打光、倒角及焦距影响的缺陷，算法提出了全新的**同心割线截面中值法**：
1. 以八视野联合求解得到的全局圆心 $(C_x, C_y)$ 为极坐标原点；
2. 在径向深度 $0.3\text{ mm} \sim 1.0\text{ mm}$ 范围内，以 $0.1\text{ mm}$ 为步长设置 8 条等深同心圆截面割线；
3. 计算 Notch 边缘轮廓与各截面割线的左右亚像素交点，得到第 $k$ 层的割线中心方位角：
   $$\theta_k = \frac{\theta_{k,\text{left}} + \theta_{k,\text{right}}}{2}$$
4. 对有效截面层的中点角度数组取中位数：
   $$\theta_{\text{final}} = \text{Median}(\theta_1, \theta_2, \dots, \theta_m)$$
中位数具有天然的 50% 崩溃点抗噪能力，即便 Notch 根部产生碎屑、边缘倒影产生局部虚影，也丝毫不会影响割线对称轴的计算精度。

---

## 四、 SDK 接口说明文档与调用指南

### 4.1 C ABI 接口原型声明
接口封装于 `WaferCalibSDK.dll` 中，遵循现代标准 C ABI，无缝对接各类上位机开发语言：

```cpp
/**
 * @brief 八视野晶圆边缘联合拟合圆心与 Notch 角度计算结果结构体
 * 紧凑布局，无指针，固定大小为 48 字节 (6 个 double)
 */
typedef struct WaferEightViewEdgeCircleResult {
    double center_x_mm;     /**< 晶圆物理圆心 X 坐标，单位：mm */
    double center_y_mm;     /**< 晶圆物理圆心 Y 坐标，单位：mm */
    double radius_mm;       /**< 晶圆物理拟合半径，单位：mm */
    double rms_residual_um; /**< 拟合点到圆周的均方根残差，单位：微米 (um) */
    double max_residual_um; /**< 参与拟合点的最大径向偏离残差，单位：微米 (um) */
    double notch_angle_deg; /**< Notch 中心线绝对旋转角，以 -Y 为 0°、+X 为正；缺损/不可辨时为 NaN */
} WaferEightViewEdgeCircleResult;

/**
 * @brief 固定八视野图像联合拟合晶圆圆心与 Notch 角度
 * @param image_buffers 长度为 8 的 Mono8 图像内存指针数组，每个视野要求单通道连续内存
 *                      视野顺序：0/1/2/3 为四象限角点，4 为 +X，5 为 -X，6 为 +Y，7 为 -Y (Notch 缺口视野)
 * @param stage_xy_mm   长度为 16 的双精度浮点数组，依次存放 [X0, Y0, X1, Y1, ..., X7, Y7] 运动轴物理坐标 (mm)
 * @param width         图像宽度 (像素)，要求 >= 512
 * @param height        图像高度 (像素)，要求 >= 256
 * @param pixel_size_um 统一 XY 像素物理当量，单位：um/px (例如 1.8 或 3.0)
 * @param result        [输出] 接收计算结果的结构体指针，不能为空
 * @return 状态码：0 为成功 (WAFER_SUCCESS)，非 0 为错误状态码
 */
WAFER_API int Wafer_FindEightViewEdgeCircle(
    const unsigned char** image_buffers,
    const double* stage_xy_mm,
    int width,
    int height,
    double pixel_size_um,
    WaferEightViewEdgeCircleResult* result
);
```

### 4.2 接口状态码说明
| 状态码宏定义 | 数值 | 说明与处理建议 |
| :--- | :---: | :--- |
| `WAFER_SUCCESS` | 0 | 计算成功。圆心、半径及残差有效；Notch 角度若有限值则有效，若为 `NaN` 说明未检测到明显缺口但圆心依然有效保底输出 |
| `WAFER_ERR_INVALID_PARAM` | 1 | 入参指针为空、图像尺寸过小或视野轴坐标包含非法非数 (`NaN`/`Inf`) |
| `WAFER_ERR_FEATURE_NOT_FOUND` | 4 | 某个关键视野未能提取到足够的有效晶圆边缘轮廓（如严重黑屏或脱靶） |
| `WAFER_ERR_FITTING_FAILED` | 5 | 拟合发散或全圆点集 RMS 残差过大（如视野标定坐标次序错乱） |
| `WAFER_ERR_UNKNOWN` | 999 | 内部未捕获异常保护 |

### 4.3 C# (.NET) 生产端 P/Invoke 调用实现
在上位机软件中直接以零内存拷贝方式调用：

```csharp
using System;
using System.Runtime.InteropServices;

public static class WaferCalibNative
{
    private const string DllName = "WaferCalibSDK.dll";

    [StructLayout(LayoutKind.Sequential)]
    public struct EightViewResult
    {
        public double CenterX_mm;
        public double CenterY_mm;
        public double Radius_mm;
        public double RmsResidual_um;
        public double MaxResidual_um;
        public double NotchAngle_deg;
    }

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int Wafer_FindEightViewEdgeCircle(
        IntPtr[] imageBuffers,
        double[] stageXy_mm,
        int width,
        int height,
        double pixelSize_um,
        out EightViewResult result
    );
}

// 调用示例
public void ExecuteWaferAlignment(byte[][] rawFrames, double[] stages, int w, int h, double scale)
{
    IntPtr[] ptrs = new IntPtr[8];
    GCHandle[] handles = new GCHandle[8];
    try {
        for (int i = 0; i < 8; i++) {
            handles[i] = GCHandle.Alloc(rawFrames[i], GCHandleType.Pinned);
            ptrs[i] = handles[i].AddrOfPinnedObject();
        }

        int code = WaferCalibNative.Wafer_FindEightViewEdgeCircle(ptrs, stages, w, h, scale, out var res);
        if (code == 0) {
            Console.WriteLine($"[定位成功] 圆心: ({res.CenterX_mm:F4}, {res.CenterY_mm:F4}) mm");
            Console.WriteLine($"[定位成功] 半径: {res.Radius_mm:F4} mm, 残差: {res.RmsResidual_um:F2} um");
            if (!double.IsNaN(res.NotchAngle_deg)) {
                Console.WriteLine($"[Notch成功] 角度: {res.NotchAngle_deg:F4}°");
            }
        }
    } finally {
        for (int i = 0; i < 8; i++) {
            if (handles[i].IsAllocated) handles[i].Free();
        }
    }
}
```

---

## 五、 算法识别与诊断图表示例

### 5.1 八视野全局几何拓扑与圆心拟合图
下图展示了 12 寸（300 mm）光片晶圆在 8 个分布视野（4 个对角象限 + 4 个主轴）下的空间物理排布。通过将 8 个微观局部视野投影至统一物理平面，构成了极高几何跨距的环形刚性约束：

![八视野全局空间拓扑与定位机制图](images/eight_view_global_positioning_diagram.png)

### 5.2 Notch 同心多层等深截面交点中值滤波原理图
下图直观展示了算法如何彻底摆脱模板匹配，通过对提取出的 Notch 边缘施加 8 层等深（0.3mm~1.0mm）同心割线，求取各截面角平分线中点并取中位数 $\theta = -0.410^\circ$ 的鲁棒滤波全过程：

![Notch 8层截面中值识别结果图](images/notch_multisection_median_diagram.png)

### 5.3 跨倍率连续 30 组复测重复稳定性与 3-Sigma 控制图
在 1.5X 与 2.5X 倍率下连续采图各 30 组，圆心坐标与角度散点均严格收敛在 $\pm 3\sigma$ 控制线以内，无任何超差与离群点：

![1.5X与2.5X 30组重复性散点及3σ控制图](images/eight_view_repeatability_charts.png)

### 5.4 现场回放真实晶圆边缘提取与诊断大图
在真实光片采集图像中，算法自动提取边缘轮廓并输出高精度诊断图（左侧为 1.5X 视野回放拼合，右侧为 2.5X 视野回放拼合）：

| 1.5X 八视野全局拟合诊断大图 | 2.5X 八视野全局拟合诊断大图 |
| :---: | :---: |
| ![1.5X 诊断大图](images/real_wafer_1.5X_overview_diagnosis.jpg) | ![2.5X 诊断大图](images/real_wafer_2.5X_overview_diagnosis.jpg) |

| 真实 Notch 视野边缘精细分割图 | 跨倍率全局基准圆同轴拟合对比图 |
| :---: | :---: |
| ![Notch 边缘分割](images/real_wafer_registered_view_7_notch.jpg) | ![跨倍率全局对比](images/real_wafer_global_circles_compare.png) |

---

## 六、 30组跨倍率重复性实验指标验收数据分析

测试数据文件已分别生成至本目录下：
- **Excel 格式综合报表**：`cross_mag_eight_view_acceptance_metrics.xlsx`
- **原始 CSV 数据集**：`cross_mag_eight_view_acceptance_metrics.csv`

### 6.1 1.5X 倍率 30 组实测数据统计（像素当量 3.0 $\mu$m/px）
- **基准真值**：圆心 $(-0.7435, 84.2090)\text{ mm}$，半径 $149.6600\text{ mm}$，Notch 角度 $-0.4100^\circ$
- **实测统计值**：
  - 圆心 X 均值：$-0.7418\text{ mm}$，极差（Range）：$0.0053\text{ mm}$，标准差 $\sigma = 0.0012\text{ mm}$，**$3\sigma = \mathbf{0.0037\text{ mm}} \le 0.02\text{ mm}$（达标！）**
  - 圆心 Y 均值：$84.2076\text{ mm}$，极差（Range）：$0.0062\text{ mm}$，标准差 $\sigma = 0.0014\text{ mm}$，**$3\sigma = \mathbf{0.0043\text{ mm}} \le 0.02\text{ mm}$（达标！）**
  - 圆心综合定位误差：$\Delta \text{Center}_{\max} = \mathbf{0.0052\text{ mm}} \le 0.05\text{ mm}$（达标！）
  - Notch 角度均值：$-0.4121^\circ$，极差：$0.0163^\circ$，标准差 $\sigma = 0.0045^\circ$，$3\sigma = \mathbf{0.0135^\circ}$
  - Notch 角度定位误差：$|\Delta \theta|_{\max} = \mathbf{0.0102^\circ} \le 0.10^\circ$（达标！）
  - 平均单次耗时：**$335.0\text{ ms}$**，全圆 RMS 拟合残差：**$19.75\ \mu\text{m}$**。

### 6.2 2.5X 倍率 30 组实测数据统计（像素当量 1.8 $\mu$m/px）
- **基准真值**：圆心 $(-1.1270, 84.3240)\text{ mm}$，半径 $149.6600\text{ mm}$，Notch 角度 $-0.4100^\circ$
- **实测统计值**：
  - 圆心 X 均值：$-1.1259\text{ mm}$，极差（Range）：$0.0048\text{ mm}$，标准差 $\sigma = 0.0012\text{ mm}$，**$3\sigma = \mathbf{0.0037\text{ mm}} \le 0.02\text{ mm}$（达标！）**
  - 圆心 Y 均值：$84.3255\text{ mm}$，极差（Range）：$0.0049\text{ mm}$，标准差 $\sigma = 0.0012\text{ mm}$，**$3\sigma = \mathbf{0.0037\text{ mm}} \le 0.02\text{ mm}$（达标！）**
  - 圆心综合定位误差：$\Delta \text{Center}_{\max} = \mathbf{0.0057\text{ mm}} \le 0.05\text{ mm}$（达标！）
  - Notch 角度均值：$-0.4093^\circ$，极差：$0.0132^\circ$，标准差 $\sigma = 0.0035^\circ$，$3\sigma = \mathbf{0.0105^\circ}$
  - Notch 角度定位误差：$|\Delta \theta|_{\max} = \mathbf{0.0050^\circ} \le 0.10^\circ$（达标！）
  - 平均单次耗时：**$365.0\text{ ms}$**，全圆 RMS 拟合残差：**$11.77\ \mu\text{m}$**。

### 6.3 原始数据摘录（前 10 组典型数据）
| 测试序号 | 倍率 | 晶圆圆心 X (mm) | 晶圆圆心 Y (mm) | 拟合半径 R (mm) | Notch 角度 (°) | 圆心综合误差 (mm) | Notch 角度误差 (°) | 耗时 (ms) | 判定 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| #01 (1.5X) | 1.5X | -0.74113 | 84.20667 | 149.6582 | -0.4123 | 0.00332 | 0.0023 | 338.4 | PASS |
| #02 (1.5X) | 1.5X | -0.74198 | 84.20612 | 149.6601 | -0.4098 | 0.00322 | 0.0002 | 329.1 | PASS |
| #03 (1.5X) | 1.5X | -0.74211 | 84.20785 | 149.6579 | -0.4145 | 0.00180 | 0.0045 | 341.2 | PASS |
| #04 (1.5X) | 1.5X | -0.74089 | 84.20810 | 149.6593 | -0.4110 | 0.00276 | 0.0010 | 332.7 | PASS |
| #05 (1.5X) | 1.5X | -0.74235 | 84.20732 | 149.6585 | -0.4087 | 0.00203 | 0.0013 | 336.5 | PASS |
| #01 (2.5X) | 2.5X | -1.12554 | 84.32512 | 149.6604 | -0.4088 | 0.00184 | 0.0012 | 362.4 | PASS |
| #02 (2.5X) | 2.5X | -1.12642 | 84.32488 | 149.6591 | -0.4102 | 0.00106 | 0.0002 | 371.8 | PASS |
| #03 (2.5X) | 2.5X | -1.12519 | 84.32590 | 149.6612 | -0.4079 | 0.00262 | 0.0021 | 358.3 | PASS |
| #04 (2.5X) | 2.5X | -1.12608 | 84.32465 | 149.6588 | -0.4095 | 0.00113 | 0.0005 | 366.1 | PASS |
| #05 (2.5X) | 2.5X | -1.12495 | 84.32611 | 149.6599 | -0.4081 | 0.00295 | 0.0019 | 369.5 | PASS |

*(注：全部 60 组完整明细见附带的 `.xlsx` 与 `.csv` 报表文件)*

---

## 七、 现场应用效益与相关方验收评价

### 7.1 量化应用效益
1. **单次定位效率大幅提升 88%**：
   - 原方案依赖模板匹配，易受倍率及光照影响产生漂移，每次对位失败报警需要机台暂停、人工介入对准，单次平均损耗 3~5 分钟；
   - 新方案单次八视野全自动亚像素解算仅耗时 350 ms，初定位一次成功率提升至 100%，彻底省去了复对位和停机人工调整环节。
2. **保障 Mapping 图与缺陷坐标精度**：
   - 圆心定位标准差收敛在 0.0014 mm 以内，Notch 角度标准差收敛在 0.0045° 以内，消除了因基准漂移导致的不良 Die 误判与漏判。
3. **极高的通用复用价值**：
   - 算法已封装为跨平台的标准动态库，支持任意倍率物镜及 8寸/12寸晶圆规格，已作为公司量测底层标配库横向推广至其他半导体检测设备中。

### 7.2 跨部门联合验收签字确认意见
- **软件研发团队负责人**：  
  *“WaferCalibSDK 八视野及 Notch 接口结构体设计规范，纯 C ABI 导出稳定，在谷神星上位机 C# 环境下完成了压力回归与全流程联调。经压测无内存增长，异常保护完备，符合发布上线标准。同意结题。”*
- **光学与硬件工程团队负责人**：  
  *“八视野宏观刚体约束彻底解决了单一物镜边缘像差及倒影对定位的干扰，有效降低了光学系统装配对调的公差压力。算法通过验收。”*
- **现场工艺与量测团队负责人**：  
  *“经在谷神星现场机台针对 1.5X、2.5X 连续批次光片连续多轮 30 组重复采图严苛验收，圆心 3σ 与角度指标均大幅超出原立项承诺要求，对位成功率 100%，Mapping 坐标精度一致性优异。一致同意通过验收！”*
