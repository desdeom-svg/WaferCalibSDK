# -*- coding: utf-8 -*-
import cv2, os, numpy as np

def track_horizontal_ridge(img, y_start_approx, x_range=(150, 3950), step=2, search_half=4):
    h, w = img.shape
    pts = []
    cur_y = float(y_start_approx)
    for x in range(x_range[0], x_range[1], step):
        y_int = int(round(cur_y))
        y0 = max(0, y_int - search_half)
        y1 = min(h, y_int + search_half + 1)
        prof = img[y0:y1, x].astype(np.float64)
        if len(prof) < 3:
            continue
        local_max = int(np.argmax(prof))
        if local_max == 0 or local_max == len(prof) - 1:
            continue
        y_m1, y_0, y_p1 = prof[local_max-1], prof[local_max], prof[local_max+1]
        denom = 2.0 * (y_m1 - 2.0*y_0 + y_p1)
        if abs(denom) > 1e-6:
            delta = (y_m1 - y_p1) / denom
            if abs(delta) <= 1.0:
                sub_y = (y0 + local_max) + delta
                if y_0 > 150:
                    pts.append((float(x), float(sub_y)))
                    cur_y = sub_y
    pts = np.array(pts)
    # 过滤偏离过大的噪点
    p = np.polyfit(pts[:, 0], pts[:, 1], 1)
    res = np.abs(pts[:, 1] - np.polyval(p, pts[:, 0]))
    pts = pts[res < 2.0]
    return pts

def track_vertical_ridge(img, x_start_approx, y_range=(150, 3950), step=2, search_half=4):
    h, w = img.shape
    pts = []
    cur_x = float(x_start_approx)
    for y in range(y_range[0], y_range[1], step):
        x_int = int(round(cur_x))
        x0 = max(0, x_int - search_half)
        x1 = min(w, x_int + search_half + 1)
        prof = img[y, x0:x1].astype(np.float64)
        if len(prof) < 3:
            continue
        local_max = int(np.argmax(prof))
        if local_max == 0 or local_max == len(prof) - 1:
            continue
        x_m1, x_0, x_p1 = prof[local_max-1], prof[local_max], prof[local_max+1]
        denom = 2.0 * (x_m1 - 2.0*x_0 + x_p1)
        if abs(denom) > 1e-6:
            delta = (x_m1 - x_p1) / denom
            if abs(delta) <= 1.0:
                sub_x = (x0 + local_max) + delta
                if x_0 > 150:
                    pts.append((float(sub_x), float(y)))
                    cur_x = sub_x
    pts = np.array(pts)
    p = np.polyfit(pts[:, 1], pts[:, 0], 1)
    res = np.abs(pts[:, 0] - np.polyval(p, pts[:, 1]))
    pts = pts[res < 2.0]
    return pts

target_dir = r"C:\Users\HYC\Desktop\畸变验证"
raw = cv2.imdecode(np.fromfile(os.path.join(target_dir, "MyImage2.bmp"), dtype=np.uint8), cv2.IMREAD_GRAYSCALE)
corr_multi = cv2.imdecode(np.fromfile(os.path.join(target_dir, "MyImage2_corrected_multi.bmp"), dtype=np.uint8), cv2.IMREAD_GRAYSCALE)
corr_single = cv2.imdecode(np.fromfile(os.path.join(target_dir, "MyImage2_corrected_single.bmp"), dtype=np.uint8), cv2.IMREAD_GRAYSCALE)

from verify_myimage2_straightness import evaluate_line_straightness

print("=== 顶部外侧水平线 (Y~62) ===")
for name, im in [('Raw', raw), ('Single', corr_single), ('Multi', corr_multi)]:
    pts = track_horizontal_ridge(im, 62.0)
    st = evaluate_line_straightness(pts, True)
    c = st['pts_count']
    r = st['rms']
    m = st['max_dev']
    pv = st['pv']
    b = st['bowing']
    print(f"{name:6s}: 点数={c}, 直线度RMS={r:.4f} px, 最大偏差={m:.4f} px, PV={pv:.4f} px, 弓高={b:.4f} px")

print("\n=== 左侧外侧垂直线 (X~64) ===")
for name, im in [('Raw', raw), ('Single', corr_single), ('Multi', corr_multi)]:
    pts = track_vertical_ridge(im, 64.0)
    st = evaluate_line_straightness(pts, False)
    c = st['pts_count']
    r = st['rms']
    m = st['max_dev']
    pv = st['pv']
    b = st['bowing']
    print(f"{name:6s}: 点数={c}, 直线度RMS={r:.4f} px, 最大偏差={m:.4f} px, PV={pv:.4f} px, 弓高={b:.4f} px")
