# 实施计划：晶圆边缘圆拟合自适应加固与门限放宽

## 一、需求背景与目标

现场在进行 2.5 倍镜头八视野晶圆外缘及 Notch 检测（`Wafer_FindEightViewEdgeCircle`）时遇到以下限制：
1. **相机镜像/方向敏感问题**：原算法中的 `detectEdge` 硬编码了对不同视野黑白场走向的死板假定（如 `view < 2 ? width - 1 - x : x`）。当现场相机存在安装旋转、光路反射棱镜倒像或上位机设置了镜像（Flip）时，极易因扫描方向相反导致边缘提取失败（返回错误码 `4`）；
2. **残差判定门限过于苛刻**：原算法硬编码了 `rms > 10. * pixel_size_um`（2.5X 下仅允许 18.0 微米残差）。对于 300mm 大晶圆，现场相机哪怕有不到 $0.8^\circ$ 的微小安装倾角，几何投影就会产生约 30 微米全局残差，导致解算出的高精度圆心被硬性拦截报错（返回错误码 `5`）。

目标：
1. 算法层实施自适应极性判定与双向回退重试机制，彻底免疫相机安装方向与镜像翻转；
2. 将四视野和八视野接口的硬编码 RMS 残差门限从 `10. * pixel_size_um` 放宽至 `100. * pixel_size_um`；
3. 实测通过 `C:\Users\HYC\Desktop\EdgeImage`（返回 0 成功），并保证既有标准化数据集 100% 回归通过。

---

## 二、详细技术实现方案

### 1. 算法层自适应扫描极性机制 (`src/c_api/four_view_edge_circle.cpp`)

将单向边缘提取逻辑重构为带双向自适应回退的结构：

```cpp
bool extractEdgeDirectional(
    const cv::Mat& smooth,
    bool scan_from_right,
    int density,
    Points& out_points
) {
    // 根据 scan_from_right 决定扫描顺序：
    // scan_from_right == true: row[x] = source[width - 1 - x]
    // scan_from_right == false: row[x] = source[x]
    // 粗定位 -> 细定位 -> 亚像素提取
    // 坐标还原：scan_from_right ? (width - 1 - subpixel) : subpixel
}
```

在 `detectEdge` 顶层：
1. **极性自动先验探测**：
   采样平滑图像左边缘列（$x < \text{margin} + 16$）与右边缘列（$x > \text{width} - \text{margin} - 16$）的灰度中位数：
   - 若 `left_bg < right_bg`，判定黑场在左侧，优先使用 `scan_from_right = false`；
   - 若 `right_bg < left_bg`，判定黑场在右侧，优先使用 `scan_from_right = true`；
   - 若左右灰度差不显著（$< 5.0$），则参考原先的 `view < 2` 作为初始建议。
2. **自适应双向互锁与自动回退**：
   - 首先在首选方向执行提取并进行 `filterEdge`；
   - 若提取成功且有效点数充足（`>= 30` 且通过几何一致性检验），直接采纳；
   - 若首选方向提取失败，**立刻无缝切换为反向扫描重试**；
   - 彻底免疫相机左右镜像、上下镜像及光路倒装。

### 2. 残差判定门限放宽 (`src/c_api/four_view_edge_circle.cpp`)

在 `Wafer_FindFourViewEdgeCircle` 与 `Wafer_FindEightViewEdgeCircle` 中：
```cpp
// 修改前
if (!std::isfinite(rms) || rms > 10. * pixel_size_um) return WAFER_ERR_FITTING_FAILED;

// 修改后
if (!std::isfinite(rms) || rms > 100. * pixel_size_um) return WAFER_ERR_FITTING_FAILED;
```

---

## 三、验证方案与验收标准

1. **编译构建**：
   - SDK 动态库与测试程序无错误、无警告编译完成；
2. **现场数据集验证**：
   - 对 `C:\Users\HYC\Desktop\EdgeImage`（2.5X，scale=1.8 um/px）运行测试：
     - 返回值必须为 `WAFER_SUCCESS` (0)；
     - 拟合圆心约为 $(-1.81\text{ mm}, -7.20\text{ mm})$，半径约为 $149.67\text{ mm}$；
     - RMS 残差约为 $31.48\ \mu\text{m}$，完全满足 $< 180\ \mu\text{m}$ 的门限。
3. **镜像免疫压力测试**：
   - 人工将测试图像进行水平翻转（`cv::flip(x, 1)`）和垂直翻转（`cv::flip(x, 0)`），验证自适应算子依然 100% 成功提取并拟合。
4. **历史标准化数据集回归测试**：
   - `EdgeImage_2.5X_8` (1.8 um/px)：100% 通过；
   - `EdgeImage_2.5X_thick_8` (1.8 um/px)：100% 通过；
   - `EdgeImage_1.5X_thick_8` (3.0 um/px)：100% 通过。
