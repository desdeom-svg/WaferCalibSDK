# 交付总结：反射率光电响应标定与灰度线性化 LUT 功能实现

本文档总结新增的“反射率光电响应标定与灰度线性化 LUT 功能 (ReflectanceLut)”模块实现细节、算法原理、基准测试结果及交付产物。

---

## 1. 业务背景与工程选型

### 1.1 业务场景
在晶圆半导体缺陷检测、微观膜厚干涉测量与高精度对位中，相机拍摄到的表面灰度直接反映了材料微观表面的物理反射率（如裸硅、二氧化硅薄膜、金属走线等）。
- **行业痛点**：由于相机 CMOS 感光芯片存在制造差异、黑电平漂移（Dark Current）、光电转换非线性（PRNU / Gamma 响应），加上光源发光强度差异与透镜边缘衰减，导致同一批次晶圆在机台 A 与机台 B 拍摄的灰度相差极大。
- **物理基准原理**：
  采用 4 块已知反射率的漫反射标准板（5%、50%、75%、90%），其理想物理线性换算值为：
  - 5% 反射率 $\to$ 灰度 13
  - 50% 反射率 $\to$ 灰度 128
  - 75% 反射率 $\to$ 灰度 192
  - 90% 反射率 $\to$ 灰度 230
- **7段死区锁定映射机制**：
  通过设置目标基准点附近的死区平台（如 $\pm 5$ 灰度），将特定反射率材质的输出严格锁定在标准阶梯灰阶，消除微小光学扰动，并在区间之间实施分段线性插值。同时支持 PCHIP 单调保形平滑样条曲线模式。

---

## 2. 核心算法设计与实现

### 2.1 模块架构
- **C++ 算法头文件**：`include/wafer_calib/modules/reflectance_lut.hpp`
- **C++ 核心实现**：`src/modules/reflectance_lut.cpp`
- **C ABI 导出层**：
  - `include/wafer_calib/c_api/wafer_calib_c.h`（新增结构体 `WaferReflectanceLutConfig`、`WaferReflectanceLutResult` 及接口 `Wafer_CalibrateReflectanceLut`、`Wafer_ApplyLutToImage`、`Wafer_SaveLutToFile`、`Wafer_LoadLutFromFile`）
  - `src/c_api/wafer_calib_c.cpp`
- **总头文件**：`include/wafer_calib/wafer_calib.hpp`

### 2.2 核心算法能力
1. **中心视场 ROI 高斯平滑直方图与亚像素波峰提取**：
   - 自动在中心区域（默认 1000×1000 ROI，可配置）统计 256 桶直方图；
   - 经高斯核平滑抑制单点噪点，结合三点抛物线插值提取亚像素级波峰位置；
2. **多模式 1D LUT 查找表动态构建**：
   - **分段阶梯死区模式**：根据手绘图规范精确构建 7 段映射区间（0~13截断、128死区、192死区、230钳位及线性过渡段）；
   - **单调平滑样条模式**：采用 PCHIP（分段三次单调 Hermite 插值）生成连续平滑曲线，杜绝等高线量化伪影；
3. **极速单帧图像查表校正 (`ApplyLut`)**：
   - 利用 CPU AVX2 SIMD 向量化指令集进行逐像素极速查表映射；
   - 实测 4096×4096（1600万像素）单帧耗时仅 **0.52 ms**；
4. **2048×1536 高分辨率四合一工业级综合诊断大看板 (`RenderDiagnosticDashboard`)**：
   - [Panel 1] 4 阶反射率输入直方图分布叠加曲线；
   - [Panel 2] 256 阶灰度 LUT 映射曲线与死区高亮带；
   - [Panel 3] 物理反射率与灰度线性度拟合优度对比（展示 $R^2_{raw}$ vs $R^2_{corr}$）；
   - [Panel 4] 计量基准数据表、分段映射规则明细与硬件性能看板。

---

## 3. 测试与验证结果

### 3.1 自动化测试执行
在 `tests/test_reflectance_lut.cpp` 中针对现场实测数据进行了端到端全流程验证：
```text
========== 开始反射率光电响应标定与灰度线性化 LUT 测试 ==========
[PASS] 成功载入 4 张标定测试图像，分辨率: 4096x4096
[PASS] 反射率 LUT 标定计算成功 (死区模式)!
   - 标定靶标 1 实测波峰: 8.31847 px, 目标物理基准: 13 px
   - 标定靶标 2 实测波峰: 147.634 px, 目标物理基准: 128 px
   - 标定靶标 3 实测波峰: 212.226 px, 目标物理基准: 192 px
   - 标定靶标 4 实测波峰: 255 px, 目标物理基准: 230 px
   - 原始非线性 R^2: 0.998451 -> 标定校正后 R^2: 0.999998
[PASS] 高分辨率综合诊断大看板已成功导出至:
   D:/Projects/opencvProject/WaferCalibSDK/images/不同反射率_线性矫正/output_reflectance_lut_diagnostic.png
   D:/Projects/opencvProject/WaferCalibSDK/docs/images/reflectance_lut_diagnostic.png
[PASS] 在线查表校正基准测试: 4096x4096 Mono8 单帧平均耗时: 0.52323 ms
[PASS] 平滑样条曲线模式测试通过 (端点单调性及连续性正常)!
[PASS] LUT 配方保存与重新载入一致性校验 100% 通过!
[PASS] C API 纯内存图像流交互测试全部通过!
========== 反射率光电响应标定与 LUT 全部自动化测试 100% 通过 ==========
```

---

## 4. 交付清单

1. **测试产物与综合诊断大图**：
   - `D:/Projects/opencvProject/WaferCalibSDK/images/不同反射率_线性矫正/output_reflectance_lut_diagnostic.png`
   - `docs/images/reflectance_lut_diagnostic.png` (2048×1536 工业深色科技看板)
2. **标定配方文件**：
   - `docs/recipe_reflectance_lut.csv` (包含 256 阶输入到输出的映射配方)
3. **交付动态链接库 DLL**：
   - `docs/WaferCalibSDK.dll` (Release x64, 大小 284 KB)
4. **接口调用说明文档 (Markdown 与 PDF)**：
   - Markdown 原文：`docs/WaferCalibSDK_C_API调用说明.md` (已增加第八单元)
   - 高清 PDF 手册：`docs/WaferCalibSDK_C_API调用说明.pdf` (26.31 MB, A4 印刷排版, 内嵌诊断图与 C# 纯内存调用代码)
5. **测试可执行程序**：
   - `build/Release/test_reflectance_lut.exe`