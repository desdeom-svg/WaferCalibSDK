# 跨双视野大基线水平标定线全局角度检测功能开发与实测记录

## 1. 任务背景与核心改进

在半导体晶圆对位与标定板调平时，用户提出针对水平标定线的大基线超高精度测角需求：
1. **输入数据**：`images/根据线输出角度/` 下的“角度调整1.bmp”（最左端起点）与“角度调整2.bmp”（最右端终点），两图分辨率均为 4096 × 4096 Mono8；
2. **机台运动特性约束**：
   - 确定机台**仅在 X 轴方向平移大跨距物理行程**，Y 轴在移动过程中严格锁定保持静止；
   - 正式**去除 `stage_delta_y_mm` 参数**，将接口入参极致精简，杜绝上层上位机误配风险；
3. **输出综合诊断看板大图规格**：
   - 确立推荐工业标准看板分辨率为 **`3000 × 1800`**（内存仅约 16.2 MB，写盘仅 828 KB，毫秒级快速生成，避免 8192×4096 并排导致的 100MB 内存爆炸和 UI 卡顿）；
   - 包含顶部 HUD 状态栏、左右两端全景与中心 1:1 亚像素特写视口、中轴落差标尺及底部参数明细看板。

---

## 2. 数学模型与实测解算数据

### 2.1 物理几何映射模型
- 视野 1 图像（左端起点，4096×4096）：提取直线亚像素方程，在中心坚线 $u_c = 2048.0\text{ px}$ 处求得直线精确高度 $v_1$；
- 视野 2 图像（右端终点，4096×4096）：提取直线亚像素方程，在中心坚线 $u_c = 2048.0\text{ px}$ 处求得直线精确高度 $v_2$；
- 像面垂直像素落差：$\Delta v = v_2 - v_1$（像素）；
- 结合垂直方向像元当量 $s_y = \text{pixel\_scale\_y\_um} / 1000.0$（mm/px，实测 $0.9009\ \mu\text{m/px}$），换算物理空间几何垂直高度差：
  $$\Delta Y_{\text{world}} = -(v_2 - v_1) \times \left(\frac{\text{pixel\_scale\_y\_um}}{1000.0}\right) \quad (\text{mm})$$
- 结合机械 X 轴移动物理位移 $\Delta X_{\text{stage}} = 285.000\text{ mm}$，解算跨两视野大基线全局倾角：
  $$\Theta_{\text{global}} = \arctan\left(\frac{\Delta Y_{\text{world}}}{\Delta X_{\text{stage}}}\right) \times \frac{180}{\pi} \quad (\text{度})$$
  $$\Theta_{\text{arcmin}} = \Theta_{\text{global}} \times 60 \quad (\text{角分})$$

### 2.2 实测机台图像解算指标

针对 `images/根据线输出角度/角度调整1.bmp` 与 `角度调整2.bmp` 实测计算指标：

| 评价维度 | 实测测量值 | 物理/几何物理意义 |
| :--- | :--- | :--- |
| **机械 X 轴移动位移** | `285.0000 mm` | 机台仅沿 X 轴从视野 1 平移至视野 2 的物理行程 |
| **Y 方向像元物理当量** | `0.9009 um/px` | 来自 9 点网格标定输出的垂直分辨率当量 |
| **视野 1 中心切线高度** | `2145.19 px` | 左端起点在 $u_c = 2048.0\text{ px}$ 处的亚像素基准高 |
| **视野 2 中心切线高度** | `403.85 px` | 右端终点在 $u_c = 2048.0\text{ px}$ 处的亚像素基准高 |
| **像面垂直落差 ($\Delta v$)** | `-1741.35 px` | 像面纵向偏移落差 ($v_2 - v_1$) |
| **工件空间垂直落差 ($\Delta Y$)** | `+1.5688 mm` | 物理世界工件在直线上测点的高度落差 |
| **单图视野 1 局部角度** | `+0.3359°` | 仅靠左侧单幅 4096 图像拟合的角度 |
| **单图视野 2 局部角度** | `+0.3143°` | 仅靠右侧单幅 4096 图像拟合的角度 |
| **局部角度一致性偏差** | `0.0215°` | 标定板局部微弯曲或边缘粗糙度引起的微弱偏差 |
| **全局超长基线绝对角度** | **`0.3154°` (`18.92` 角分)** | **跨越 285 mm 超长基线的最优高精度全局角度** |
| **基线抗噪放大比增益** | **`~77.2 倍`** | **相比单视野 3.69 mm 视场，测角抗离散噪声能力提升 77 倍！** |

---

## 3. C API 导出接口与 C# 签名

在 `include/wafer_calib/c_api/wafer_calib_c.h` 中导出纯平铺、无句柄 C 接口：

```c
WAFER_API int Wafer_FindTwoViewHorizontalLineAngle(
    const unsigned char* mono8_view1,    // 视野 1 (最左端起点) Mono8 图像指针
    const unsigned char* mono8_view2,    // 视野 2 (最右端终点) Mono8 图像指针
    int width,                           // 图像宽度 (如 4096)
    int height,                          // 图像高度 (如 4096)
    double stage_delta_x_mm,             // 机械 X 轴移动物理位移 (mm，如 285.000)
    double pixel_scale_y_um,             // 像元垂直物理当量 (um/px，如 0.9009)
    double* out_global_angle_deg,        // 输出：跨双视野大基线超高精度全局角度 (度)
    double* out_view1_angle_deg,         // 可选输出：视野 1 单图局部拟合角度 (度，传 NULL 忽略)
    double* out_view2_angle_deg,         // 可选输出：视野 2 单图局部拟合角度 (度，传 NULL 忽略)
    unsigned char* diagnostic_bgr,       // 可选输出：综合诊断看板大图缓冲区 (传 NULL 不生成)
    int diag_width,                      // 诊断大图宽度 (推荐 3000)
    int diag_height                      // 诊断大图高度 (推荐 1800)
);
```

### C# P/Invoke 原生互操作代码：

```csharp
[DllImport("WaferCalibSDK.dll", CallingConvention = CallingConvention.Cdecl)]
public static extern int Wafer_FindTwoViewHorizontalLineAngle(
    IntPtr mono8View1,
    IntPtr mono8View2,
    int width,
    int height,
    double stageDeltaXMm,
    double pixelScaleYUm,
    out double outGlobalAngleDeg,
    out double outView1AngleDeg,
    out double outView2AngleDeg,
    IntPtr diagnosticBgr,
    int diagWidth,
    int diagHeight
);
```

---

## 4. 交付产物与验证成果

1. **核心算法与头文件**：
   - `include/wafer_calib/modules/line_angle.hpp`：声明 `TwoViewLineAngleResult` 结构体与 `findTwoViewHorizontalLineAngle`；
   - `src/modules/line_angle.cpp`：实现亚像素中心切线高度提取、双视野大基线联合求解与 `3000 × 1800` 工业看板合成；
2. **C 导出接口**：
   - `include/wafer_calib/c_api/wafer_calib_c.h`：声明 `Wafer_FindTwoViewHorizontalLineAngle`；
   - `src/c_api/wafer_calib_c.cpp`：实现纯平铺 C 导出接口与异常保护；
3. **自动化测试套件**：
   - `tests/test_two_view_line_angle.cpp`：
     - 测试 1：合成图像标称基准验证（0.5000° 标称，解算 0.4999°，误差 < 0.0001°）；
     - 测试 2：实测两端 4096×4096 图像标定（解算全局角度 0.3154°，即 18.92 角分）；
     - 测试 3：C API 纯平铺接口数据一致性校验；
     - 测试 4：零位移、零当量、空指针等边界防御；
     - 全部测试 100% 通过（PASS）；
4. **编译与二进制库**：
   - Release x64 `WaferCalibSDK.dll` (401 KB) 与 `WaferCalibSDK.lib` (283 KB) 编译通过并已拷贝至 `docs/`；
5. **综合诊断大图产物**：
   - `images/根据线输出角度/two_view_line_angle_diag_3000x1800.jpg` 与 `docs/images/two_view_line_angle_diag_3000x1800.jpg` 生成完毕；
6. **文档更新**：
   - `docs/WaferCalibSDK_C_API调用说明.md` 补充第 4.5 与 4.6 节详细技术说明、C# 示例及诊断图展示；
   - `docs/WaferCalibSDK_C_API调用说明.pdf`（28.42 MB）通过 Edge 无头打印引擎重新生成完毕，排版精美。