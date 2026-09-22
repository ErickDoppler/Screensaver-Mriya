#!/usr/bin/env python3
"""Measures the fuselage's cheatline out of the AN225.fbx model's texture and
writes it as a table the shader draws from, crisp at any distance.

    python tools/make_bands.py

Reads  res/models/Gearcover1_diff.png   (that model's fuselage texture)
Writes src/bands_table.h                 (compiled in)

That model paints its fuselage sides with a planar projection, fitted to its
UVs and carried over to ours (the constants below). Along every texture
column the tool finds the yellow band, the blue band and the top of the grey
belly - tracking each band from the middle of the fuselage outwards, so the
titles' blue letters are not taken for the band - converts them to body
coordinates, and smooths the edges along the fuselage. Where the texture's
bands end (the tail) they taper to nothing; forward of its nose they hold.

The table: FUS_BAND_N rows, one every 0.5 m from z = -41, each the heights
(body metres) of the yellow band's top and bottom, the blue band's top and
bottom, and the grey belly's top. Needs numpy and Pillow.
"""
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SRC = os.path.join(ROOT, "res", "models", "Gearcover1_diff.png")
OUT = os.path.join(ROOT, "src", "bands_table.h")

# The left side's projection, fitted to the FBX model's UVs:
#   u = CU_Z z + CU_Y y + CU_X |x| + CU_0 ;  v = CV_Y y + CV_0  (v up the image)
CU_Z, CU_Y, CU_X, CU_0 = -0.013241, -0.000755, -0.003225, 0.466760
CV_Y, CV_0 = 0.013462, 0.940742
AX = 3.2                       # a typical |x| on the side
ROWS = (0, 127)                # the left side's strip in the texture
Z0, STEP, N = -41.0, 0.5, 170


def py_to_y(py):
    return ((1.0 - (py + 0.5) / 1024.0) - CV_0) / CV_Y


def px_to_z(px):
    return ((px + 0.5) / 1024.0 - CU_0 - CU_X * AX) / CU_Z


def runs(mask):
    out, s = [], None
    for i, m in enumerate(list(mask) + [False]):
        if m and s is None:
            s = i
        elif not m and s is not None:
            out.append((s, i))
            s = None
    return out


def track(mask, start):
    """The band's run in every column, followed from `start` outward: in each
    column the longest run near where it was in the last one."""
    r0, r1 = ROWS
    first = max((x for x in runs(mask[r0:r1, start]) if x[1] - x[0] >= 3), key=lambda x: x[1] - x[0])
    res = {start: first}
    for rng in (range(start + 1, 1000), range(start - 1, 59, -1)):
        p = (first[0] + first[1]) / 2
        for px in rng:
            best = None
            for a, c in runs(mask[r0:r1, px]):
                if c - a < 2 or abs((a + c) / 2 - p) > 6:
                    continue
                if best is None or c - a > best[1] - best[0]:
                    best = (a, c)
            if best:
                res[px] = best
                p = (best[0] + best[1]) / 2
    return res


def smooth(z, v, zq, sigma=1.2):
    ok = ~np.isnan(v)
    zz, vv = z[ok], v[ok]
    o = np.argsort(zz)
    zz, vv = zz[o], vv[o]
    med = np.array([np.median(vv[max(0, i - 7):i + 8]) for i in range(len(vv))])
    out = []
    for q in zq:
        w = np.exp(-0.5 * ((zz - q) / sigma) ** 2)
        out.append((w * med).sum() / w.sum() if w.sum() > 1e-3 else np.nan)
    return np.array(out), zz.min(), zz.max()


def main():
    im = np.asarray(Image.open(SRC).convert("RGB")).astype(float)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    yel_m = (r > 150) & (g > 120) & (b < 130) & (r - b > 70)
    blu_m = (b - r > 45) & (b > 90)
    wht = (r > 225) & (g > 225) & (b > 225)
    gry_m = (r > 140) & (r < 222) & (np.abs(r - g) < 20) & (b - r < 30) & ~wht

    yel = track(yel_m, 500)            # column 500: mid fuselage, clean bands
    blu = track(blu_m, 500)
    r0, r1 = ROWS
    grey = {}
    for px in range(60, 1000):
        lows = [v[1] for v in (yel.get(px), blu.get(px)) if v]
        if not lows:
            continue
        for py in range(r0 + max(lows), r1 - 3):
            if gry_m[py:py + 3, px].all():
                grey[px] = py
                break

    cols = np.arange(60, 1000)
    z = np.array([px_to_z(px) for px in cols])
    zq = Z0 + STEP * np.arange(N)

    def edge(d, i):
        v = np.array([py_to_y(r0 + d[px][i]) if px in d else np.nan for px in cols])
        return smooth(z, v, zq)

    yt, ylo, yhi = edge(yel, 0)
    yb, _, _ = edge(yel, 1)
    bt, blo, bhi = edge(blu, 0)
    bb, _, _ = edge(blu, 1)
    gv = np.array([py_to_y(grey[px]) if px in grey else np.nan for px in cols])
    gt, glo, ghi = smooth(z, gv, zq)

    def finish(top, bot, lo, hi):
        """Hold forward of the measurements; taper to nothing past their end."""
        top, bot = top.copy(), bot.copy()
        k_lo = np.argmin(np.abs(zq - lo))
        k_hi = np.argmin(np.abs(zq - hi))
        top[:k_lo], bot[:k_lo] = top[k_lo], bot[k_lo]
        for k in range(k_hi, N):
            t = np.clip((zq[k] - hi) / 1.5, 0.0, 1.0)
            mid = 0.5 * (top[min(k, k_hi)] + bot[min(k, k_hi)])
            half = 0.5 * (top[min(k, k_hi)] - bot[min(k, k_hi)]) * (1.0 - t)
            top[k], bot[k] = mid + half, mid - half
        return top, bot

    yt, yb = finish(yt, yb, ylo, yhi)
    bt, bb = finish(bt, bb, blo, bhi)
    k_lo, k_hi = np.argmin(np.abs(zq - glo)), np.argmin(np.abs(zq - ghi))
    gt[:k_lo] = gt[k_lo]
    gt[k_hi:] = gt[k_hi]

    with open(OUT, "w", newline="\n") as f:
        f.write("/* Generated by tools/make_bands.py - do not edit.\n"
                " * The fuselage's cheatline, measured from the AN225.fbx texture: per row\n"
                " * (one every FUS_BAND_STEP metres from FUS_BAND_Z0) the heights, body\n"
                " * metres, of the yellow band's top and bottom, the blue band's top and\n"
                " * bottom, and the grey belly's top. */\n")
        f.write("#ifndef MR_BANDS_TABLE_H\n#define MR_BANDS_TABLE_H\n")
        f.write(f"#define FUS_BAND_N {N}\n#define FUS_BAND_Z0 ({Z0:.1f}f)\n#define FUS_BAND_STEP {STEP:.1f}f\n")
        f.write("static const float fus_band_table[FUS_BAND_N][5] = {\n")
        for k in range(N):
            f.write("    { %7.3ff, %7.3ff, %7.3ff, %7.3ff, %7.3ff },\n" % (yt[k], yb[k], bt[k], bb[k], gt[k]))
        f.write("};\n#endif\n")
    print(f"{OUT}: {N} rows; yellow z {ylo:.1f}..{yhi:.1f}, blue z {blo:.1f}..{bhi:.1f}, grey z {glo:.1f}..{ghi:.1f}")


if __name__ == "__main__":
    main()
