import cv2, os, numpy as np

img_dir = r"C:\Users\HYC\Desktop\EdgeImage"
files = [
    "0_103.8243_99.38033.bmp",
    "1_103.8243_-112.7517.bmp",
    "2_-108.3077_99.38033.bmp",
    "3_-108.3077_-112.7517.bmp",
    "4_147.7583_-6.68569.bmp",
    "5_-152.2417_-6.68569.bmp",
    "6_-2.2417_143.3143.bmp",
    "7_-2.2417_-156.6857.bmp"
]

stage_xy_mm = [
    103.8243, 99.38033,
    103.8243, -112.7517,
    -108.3077, 99.38033,
    -108.3077, -112.7517,
    147.7583, -6.68569,
    -152.2417, -6.68569,
    -2.2417, 143.3143,
    -2.2417, -156.6857
]

pixel_size_um = 1.8
scale = pixel_size_um / 1000.0

def quantile(v, q):
    return float(np.percentile(v, q * 100))

def filterEdge(points, height):
    if len(points) < 30: return False, []
    retained = list(points)
    for it in range(4):
        a = np.zeros((len(retained), 3))
        b = np.zeros((len(retained), 1))
        for j in range(len(retained)):
            y = (retained[j][1] - height * 0.5) / height
            a[j, 0] = 1.0; a[j, 1] = y; a[j, 2] = y * y
            b[j, 0] = retained[j][0]
        c, _, _, _ = np.linalg.lstsq(a, b, rcond=None)
        c0, c1, c2 = c[0, 0], c[1, 0], c[2, 0]
        residuals = []
        for p in retained:
            y = (p[1] - height * 0.5) / height
            residuals.append(p[0] - (c0 + c1 * y + c2 * y * y))
        med = float(np.median(residuals))
        errors = [abs(e - med) for e in residuals]
        limit = max(3.0, 4.0 * 1.4826 * float(np.median(errors)))
        new_retained = []
        for p in points:
            y = (p[1] - height * 0.5) / height
            res = p[0] - (c0 + c1 * y + c2 * y * y)
            if abs(res - med) < limit:
                new_retained.append(p)
        retained = new_retained
        if len(retained) < 30: return False, []
    ys = [p[1] for p in retained]
    if max(ys) - min(ys) < height * 0.20:
        return False, []
    return True, retained

def extractEdgeDirectional(smooth, scan_from_right, do_filter, density):
    height, width = smooth.shape
    margin = max(16, height // 32)
    step = max(1, height // density)
    points = []
    for y in range(margin, height - margin, step):
        row = smooth[y, :]
        if scan_from_right:
            row = row[::-1]
        bg = quantile(row, 0.10)
        signal = quantile(row, 0.85)
        if signal - bg < 5.0:
            continue
        coarseThreshold = bg + 0.40 * (signal - bg)
        run = 0
        coarse = -1
        for x in range(width):
            if row[x] > coarseThreshold:
                run += 1
            else:
                run = 0
            if run == 15:
                coarse = x - 14
                break
        if coarse < 130 or coarse >= width - 130:
            continue
        exterior = row[coarse - 120 : coarse - 60]
        interior = row[coarse + 30 : coarse + 100]
        b = quantile(exterior, 0.50)
        s = quantile(interior, 0.50)
        if s - b < 5.0:
            continue
        noise = [abs(v - b) for v in exterior]
        sigma = 1.4826 * quantile(noise, 0.50)
        threshold = b + 0.15 * (s - b)
        if threshold - b < 3.0 * sigma or s >= 254.0:
            continue
        for x in range(coarse - 100, coarse + 30):
            if row[x] <= threshold and row[x+1] > threshold:
                subpixel = x + (threshold - row[x]) / (row[x+1] - row[x])
                actual_x = (width - 1.0 - subpixel) if scan_from_right else subpixel
                points.append((actual_x, float(y)))
                break
    if do_filter:
        ok, filtered = filterEdge(points, height)
        return ok, filtered
    return len(points) >= 30, points

def detectEdge(img, view, do_filter=True, density=256):
    smooth = cv2.GaussianBlur(img.astype(np.float32), (0, 0), 2.0)
    height, width = smooth.shape
    check_margin = max(16, height // 32)
    check_cols = min(64, width // 8)
    left_samples = smooth[check_margin:height-check_margin:8, :check_cols].flatten()
    right_samples = smooth[check_margin:height-check_margin:8, width-check_cols:].flatten()
    left_median = float(np.median(left_samples))
    right_median = float(np.median(right_samples))
    preferred_scan_from_right = (view < 2)
    if abs(right_median - left_median) >= 5.0:
        preferred_scan_from_right = (right_median < left_median)
    ok, pts = extractEdgeDirectional(smooth, preferred_scan_from_right, do_filter, density)
    if not ok:
        ok, pts = extractEdgeDirectional(smooth, not preferred_scan_from_right, do_filter, density)
    return ok, pts

def toWorld(p, stage, width, height, scale):
    return (stage[0] + (p[0] - width * 0.5) * scale,
            stage[1] - (p[1] - height * 0.5) * scale)

def fitCircle(groups):
    N = len(groups)
    total_count = sum(len(g) for g in groups)
    origin_x = sum(sum(p[0] for p in g) / (N * len(g)) for g in groups)
    origin_y = sum(sum(p[1] for p in g) / (N * len(g)) for g in groups)
    
    a = np.zeros((total_count, 3))
    b = np.zeros((total_count, 1))
    row = 0
    for g in groups:
        w = 1.0 / np.sqrt(len(g))
        for p in g:
            px = p[0] - origin_x
            py = p[1] - origin_y
            a[row, 0] = 2.0 * px * w
            a[row, 1] = 2.0 * py * w
            a[row, 2] = w
            b[row, 0] = (px * px + py * py) * w
            row += 1
    
    c, _, _, _ = np.linalg.lstsq(a, b, rcond=None)
    cx, cy = c[0, 0], c[1, 0]
    r2 = c[2, 0] + cx * cx + cy * cy
    if r2 <= 0: return False, None
    r = np.sqrt(r2)
    
    circle = [cx, cy, r]
    converged = False
    for it in range(30):
        row = 0
        a = np.zeros((total_count, 3))
        b = np.zeros((total_count, 1))
        for g in groups:
            w = 1.0 / np.sqrt(len(g))
            for p in g:
                x = circle[0] - (p[0] - origin_x)
                y = circle[1] - (p[1] - origin_y)
                d = np.hypot(x, y)
                if d < 1e-12: return False, None
                a[row, 0] = x / d * w
                a[row, 1] = y / d * w
                a[row, 2] = -w
                b[row, 0] = (circle[2] - d) * w
                row += 1
        delta, _, _, _ = np.linalg.lstsq(a, b, rcond=None)
        circle[0] += delta[0, 0]
        circle[1] += delta[1, 0]
        circle[2] += delta[2, 0]
        if np.linalg.norm(delta) < 1e-10:
            converged = True
            break
    circle[0] += origin_x
    circle[1] += origin_y
    return converged, circle

im0 = cv2.imread(os.path.join(img_dir, files[0]), cv2.IMREAD_GRAYSCALE)
height, width = im0.shape

seedGroups = []
for i in range(7):
    im = cv2.imread(os.path.join(img_dir, files[i]), cv2.IMREAD_GRAYSCALE)
    scan = np.transpose(im) if i == 6 else im
    direction = 0 if (i < 2 or i == 4) else 2
    ok, pts = detectEdge(scan, direction)
    world_pts = []
    for p in pts:
        px, py = (p[1], p[0]) if i == 6 else (p[0], p[1])
        st = stage_xy_mm[2*i : 2*i+2]
        world_pts.append(toWorld((px, py), st, width, height, scale))
    seedGroups.append(world_pts)

ok_seed, circle = fitCircle(seedGroups)
print("Seed circle fit:", ok_seed, circle)

# View 7:
im7 = cv2.imread(os.path.join(img_dir, files[7]), cv2.IMREAD_GRAYSCALE)
scan7 = np.transpose(im7)
ok7, raw = detectEdge(scan7, 0, do_filter=False, density=1024)
print("View 7 raw detectEdge:", ok7, len(raw))

# In C++:
# double left = width, right = -1.;
# for (auto& p : raw) {
#     std::swap(p.x, p.y);
#     const auto q = toWorld(p, stage_xy_mm + 14, width, height, scale);
#     if (circle[2] - std::hypot(q.x - circle[0], q.y - circle[1]) > std::max(.1, 10. * scale)) {
#         left = std::min(left, p.x); right = std::max(right, p.x);
#     }
# }
left = width
right = -1.0
raw_swapped = []
for p in raw:
    sp = (p[1], p[0]) # swap x, y
    raw_swapped.append(sp)
    q = toWorld(sp, stage_xy_mm[14:16], width, height, scale)
    depth = circle[2] - np.hypot(q[0] - circle[0], q[1] - circle[1])
    if depth > max(0.1, 10.0 * scale):
        left = min(left, sp[0])
        right = max(right, sp[0])

print(f"Notch detected left={left}, right={right}")

# In C++:
# for (const auto& p : raw) // remember raw points have p.x and p.y swapped in previous loop!
#     if (right < 0. || p.x < left - 60. || p.x > right + 60.)
#         normal.emplace_back(p.y, p.x);
# if (!filterEdge(normal, width)) return WAFER_ERR_FEATURE_NOT_FOUND;
normal = []
for p in raw_swapped:
    if right < 0.0 or p[0] < left - 60.0 or p[0] > right + 60.0:
        normal.append((p[1], p[0])) # normal points are in scan coords (height=width of image)

ok_filter, filtered_normal = filterEdge(normal, width)
print("Normal points filtered:", ok_filter, len(filtered_normal))

# Now all 8 groups:
groups = list(seedGroups)
group7 = []
for p in filtered_normal:
    # toWorld({p.y, p.x}, stage_xy_mm + 14, width, height, scale)
    group7.append(toWorld((p[1], p[0]), stage_xy_mm[14:16], width, height, scale))
groups.append(group7)

ok_final, final_circle = fitCircle(groups)
print("Final circle fit:", ok_final, final_circle)

mse = 0.0
maximum = 0.0
for g in groups:
    s = 0.0
    for p in g:
        e = np.hypot(p[0] - final_circle[0], p[1] - final_circle[1]) - final_circle[2]
        s += e * e
        maximum = max(maximum, abs(e))
    mse += s / (8.0 * len(g))

rms = np.sqrt(mse) * 1000.0
print(f"RMS: {rms:.4f} um, Max residual: {maximum*1000.0:.4f} um")
print(f"Threshold: 100 * {pixel_size_um} = {100.0 * pixel_size_um}")
