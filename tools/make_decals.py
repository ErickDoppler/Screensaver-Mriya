#!/usr/bin/env python3
"""Builds the livery atlas from the kit decal sheet.

    python tools/make_decals.py

Reads  res/textures/decals_reference.png  (the BSmodelle 1/250 An-225 sheet)
Writes res/textures/decals.tex            (embedded in the .scr)
       src/decals_atlas.h                 (where each decal landed in the atlas)

Each decal used on the aircraft is cut out of the sheet, upscaled 3x with a
Lanczos filter so its curves stay round when the camera is a metre from the
skin, and its white paper is turned into transparency: a pixel's coverage is
how far it is from white, and its colour is un-mixed from the white it was
blended with, then bled outwards so the anti-aliased rim does not go pale when
the shader sharpens the edge.

The .tex format is run-length coded RGBA, because a decal is mostly a few flat
colours and 16 MB of raw texels would triple the size of the screensaver:
    char magic[4] = "MRT1"; u32 width, height;
    per row: u16 run_count; { u16 length; u8 r, g, b, a; } runs[run_count]
Needs numpy and Pillow.
"""
import os
import struct

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SRC = os.path.join(ROOT, "res", "textures", "decals_reference.png")
OUT = os.path.join(ROOT, "res", "textures", "decals.tex")
HDR = os.path.join(ROOT, "src", "decals_atlas.h")

SCALE = 3
PAD = 6
ATLAS_W = 2048

# Stencils that sit inside a decal's box on the sheet but are not part of it.
EXCLUDE = [(3, 120, 45, 198), (150, 235, 212, 272), (105, 380, 156, 456)]

# Paint names, for saying which colours a decal is allowed to use.
BLUE, NAVY, YELLOW, BLACK, GREY, RED, CYAN, LIGHT, WHITE = range(9)
STRIPES = (BLUE, YELLOW)

# name, (x0, y0, x1, y1) on the sheet, inclusive, and the paints it is made
# of. Restricting the palette per decal matters at the edges: an
# anti-aliased blue pixel un-mixed from the paper can land nearer black or
# navy than blue, and a stripe grows dark dashes along its rim.
CROPS = [
    ("FIN_A",    (0, 0, 121, 95), STRIPES),       # fin swoosh
    ("FIN_B",    (0, 156, 126, 251), STRIPES),    # fin swoosh, the other hand
    ("ENG_R",    (131, 75, 215, 104), STRIPES),   # engine nacelle swoosh
    ("ENG_L",    (45, 95, 129, 124), STRIPES),    # engine nacelle swoosh, the other hand
    ("ROUNDEL",  (376, 0, 435, 53), None),        # ANTONOV AIRLINES roundel: every colour
    ("FLAG",     (378, 61, 417, 82), STRIPES),    # the Ukrainian flag
]

# The lettering is set in type rather than cut from the sheet: at 1/250 the
# sheet's letters are a few pixels tall, and on a 6 m title they turn to
# blots. Each is a list of lines: (text, font file, size relative to the
# first line, paint).
FONTS = "C:/Windows/Fonts"
TEXTS = [
    ("AN225", [("ANTONOV 225", "arialbd.ttf", 1.0, BLUE)]),
    ("ICT", [("INTERNATIONAL CARGO", "arialbd.ttf", 1.0, BLUE),
             ("TRANSPORTER", "arialbd.ttf", 1.0, BLUE),
             ("Phone: +38(044) 454-29-60   Fax: +38(044) 454-29-52", "arialbd.ttf", 0.42, NAVY),
             ("E-mail: sales@antonov.kiev.ua", "arialbd.ttf", 0.42, NAVY)]),
    ("MRIYA", [("МРІЯ", "arialbi.ttf", 1.0, BLUE)]),
    ("REG", [("UR-82060", "arialbd.ttf", 1.0, BLACK)]),
    ("CHIN", [("ANTONOV", "arialbd.ttf", 1.0, BLUE),
              ("DESIGN  BUREAU", "arialbd.ttf", 0.62, BLUE),
              ("INTERNATIONAL  CARGO", "arialbd.ttf", 0.62, BLUE),
              ("TRANSPORTER", "arialbd.ttf", 0.62, BLUE),
              ("Phone:+38(044)454-28-60", "arialbd.ttf", 0.45, BLUE),
              ("Fax:+38(044)454-28-52", "arialbd.ttf", 0.45, BLUE),
              ("E-mail:sales@antonov.kiev.ua", "arialbd.ttf", 0.45, BLUE)]),
]
TEXT_PX = 72        # the first line's cap height in atlas texels


def set_text(lines):
    """Renders the lines centred under one another, 4x supersampled.
    Returns colour and coverage at atlas resolution, trimmed to the ink."""
    from PIL import ImageDraw, ImageFont
    ss = 4
    rendered = []
    for text, font, rel, paint in lines:
        f = ImageFont.truetype(os.path.join(FONTS, font), int(TEXT_PX * 1.38 * rel * ss))
        l, t, r, b = f.getbbox(text)
        img = Image.new("L", (r - l + 8 * ss, b - t + 8 * ss), 0)
        ImageDraw.Draw(img).text((4 * ss - l, 4 * ss - t), text, font=f, fill=255)
        rendered.append((np.asarray(img, np.float32) / 255.0, PALETTE[paint][1], rel))
    gap = int(TEXT_PX * 0.32 * ss)
    w = max(r.shape[1] for r, _, _ in rendered)
    h = sum(r.shape[0] for r, _, _ in rendered) + gap * (len(rendered) - 1)
    a = np.zeros((h, w), np.float32)
    col = np.zeros((h, w, 3), np.float32)
    y = 0
    for cov, paint, rel in rendered:
        x = (w - cov.shape[1]) // 2
        a[y:y + cov.shape[0], x:x + cov.shape[1]] = np.maximum(a[y:y + cov.shape[0], x:x + cov.shape[1]], cov)
        col[y:y + cov.shape[0], x:x + cov.shape[1]][cov > 0.0] = paint
        y += cov.shape[0] + int(gap * rel)
    h = (a.shape[0] // ss) * ss; w = (a.shape[1] // ss) * ss
    a = a[:h, :w].reshape(h // ss, ss, w // ss, ss).mean(axis=(1, 3))
    col = col[:h:ss, :w:ss]
    ys, xs = np.nonzero(a > 0.02)
    y0, y1, x0, x1 = max(ys.min() - 3, 0), ys.max() + 4, max(xs.min() - 3, 0), xs.max() + 4
    a, col = a[y0:y1, x0:x1], col[y0:y1, x0:x1]
    col = bleed(col, a)
    a = np.round(a * 31.0) / 31.0
    return col, a


def load_sheet():
    im = np.asarray(Image.open(SRC).convert("RGB")).astype(np.float32) / 255.0
    return im


def to_rgba(crop):
    """White paper -> transparency. Returns float RGBA, straight alpha."""
    dist = 1.0 - crop.min(axis=2)                  # how far from white
    # The scan's paper is not quite white: ignore the faint noise on it.
    a = np.clip((dist - 0.10) * 1.4, 0.0, 1.0)
    safe = np.maximum(a, 1e-3)[..., None]
    col = np.clip((crop - (1.0 - a[..., None])) / safe, 0.0, 1.0)
    return col, a


def bleed(col, a, iters=12):
    """Pushes the solid colours outwards into the transparent margin."""
    known = a > 0.45
    col = col.copy()
    for _ in range(iters):
        acc = np.zeros_like(col)
        cnt = np.zeros(a.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sh_k = np.roll(known, (dy, dx), axis=(0, 1))
            sh_c = np.roll(col, (dy, dx), axis=(0, 1))
            acc += sh_c * sh_k[..., None]
            cnt += sh_k
        grow = (~known) & (cnt > 0)
        col[grow] = acc[grow] / cnt[grow][:, None]
        known = known | grow
    return col


# Sheet colours (as printed on the kit sheet, after un-mixing from the paper)
# and the paint each one stands for. The sheet prints Antonov blue as a pure
# screen blue; the aircraft wore something deeper.
PALETTE = [
    ((0.08, 0.08, 0.94), (0.00, 0.31, 0.71)),   # blue cheatline and titles
    ((0.08, 0.08, 0.60), (0.06, 0.18, 0.47)),   # navy lettering
    ((0.99, 0.89, 0.08), (1.00, 0.81, 0.00)),   # yellow
    ((0.08, 0.08, 0.08), (0.10, 0.10, 0.11)),   # black
    ((0.38, 0.38, 0.38), (0.27, 0.27, 0.29)),   # grey registration
    ((0.90, 0.08, 0.08), (0.78, 0.08, 0.10)),   # red
    ((0.12, 0.82, 0.88), (0.16, 0.67, 0.86)),   # roundel cyan
    ((0.59, 0.59, 0.98), (0.47, 0.59, 0.90)),   # light blue
    ((1.00, 1.00, 1.00), (1.00, 1.00, 1.00)),   # white inside the roundel
]


def snap(col, allowed=None):
    """Every texel becomes exactly one paint colour, which is what lets the
    run-length coding work - and it is what the aircraft wore anyway."""
    idx = list(range(len(PALETTE))) if allowed is None else list(allowed)
    src = np.array([PALETTE[i][0] for i in idx], np.float32)
    dst = np.array([PALETTE[i][1] for i in idx], np.float32)
    d = ((col[..., None, :] - src[None, None]) ** 2).sum(-1)
    return dst[d.argmin(-1)]


def soften(a, sigma=0.8):
    """A light blur of the coverage at the sheet's own resolution. The scan's
    edges step pixel by pixel; blurred, they become a smooth ramp whose middle
    runs along the true outline, and the shader's edge sharpening then draws
    straight lines and round curves instead of a staircase - which on a fin
    ten metres tall is a staircase of 9 cm steps."""
    r = 3
    x = np.arange(-r, r + 1, dtype=np.float32)
    k = np.exp(-x * x / (2 * sigma * sigma))
    k /= k.sum()
    pad = np.pad(a, r, mode="edge")
    tmp = np.zeros((a.shape[0] + 2 * r, a.shape[1]), np.float32)
    for i in range(2 * r + 1):
        tmp += k[i] * pad[:, i:i + a.shape[1]]
    out = np.zeros(a.shape, np.float32)
    for i in range(2 * r + 1):
        out += k[i] * tmp[i:i + a.shape[0], :]
    return out


def upscale_nearest(arr):
    return arr.repeat(SCALE, axis=0).repeat(SCALE, axis=1)


def upscale(arr):
    h, w = arr.shape[:2]
    out = []
    chans = arr.shape[2] if arr.ndim == 3 else 1
    for c in range(chans):
        ch = arr[..., c] if arr.ndim == 3 else arr
        img = Image.fromarray(ch.astype(np.float32), mode="F")
        img = img.resize((w * SCALE, h * SCALE), Image.LANCZOS)
        out.append(np.asarray(img))
    res = np.stack(out, axis=2) if chans > 1 else out[0]
    return np.clip(res, 0.0, 1.0)


def label_blobs(sheet):
    """Connected blobs of ink, grown by a pixel to close anti-aliasing gaps.
    Returns the label image and each label's bounding box."""
    ink = (1.0 - sheet.min(axis=2)) > 0.12
    grown = ink.copy()
    for _ in range(1):
        g = grown.copy()
        g[1:] |= grown[:-1]; g[:-1] |= grown[1:]
        g[:, 1:] |= grown[:, :-1]; g[:, :-1] |= grown[:, 1:]
        grown = g
    h, w = grown.shape
    lab = np.zeros((h, w), np.int32)
    boxes = {}
    n = 0
    for sy in range(h):
        for sx in range(w):
            if not grown[sy, sx] or lab[sy, sx]:
                continue
            n += 1
            stack = [(sy, sx)]
            lab[sy, sx] = n
            x0 = x1 = sx
            y0 = y1 = sy
            while stack:
                cy, cx = stack.pop()
                x0 = min(x0, cx); x1 = max(x1, cx); y0 = min(y0, cy); y1 = max(y1, cy)
                for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                    if 0 <= ny < h and 0 <= nx < w and grown[ny, nx] and not lab[ny, nx]:
                        lab[ny, nx] = n
                        stack.append((ny, nx))
            boxes[n] = (x0, y0, x1, y1)
    return lab, boxes


def main():
    sheet = load_sheet()
    lab, boxes = label_blobs(sheet)
    tiles = []
    for name, (x0, y0, x1, y1), allowed in CROPS:
        crop = sheet[y0:y1 + 1, x0:x1 + 1]
        col, a = to_rgba(crop)
        # Keep only the blobs that lie inside this crop: the sheet packs its
        # decals tightly, and a neighbour's corner would be painted on too.
        own = np.zeros(a.shape, bool)
        sub = lab[y0:y1 + 1, x0:x1 + 1]
        for k in np.unique(sub):
            if k == 0:
                continue
            bx0, by0, bx1, by1 = boxes[k]
            inside = bx0 >= x0 - 3 and by0 >= y0 - 3 and bx1 <= x1 + 3 and by1 <= y1 + 3
            excluded = any(bx0 >= ex0 and by0 >= ey0 and bx1 <= ex1 and by1 <= ey1
                           for ex0, ey0, ex1, ey1 in EXCLUDE)
            if inside and not excluded:
                own |= sub == k
        a = a * own
        col = snap(bleed(col, a), allowed)
        col = upscale_nearest(col)
        a = upscale(soften(a))
        # Lanczos rings: flatten the near-solid and near-empty ends, and keep
        # 32 steps across the edge, which is plenty for a rim a few texels wide.
        a = np.where(a > 0.94, 1.0, np.where(a < 0.06, 0.0, a))
        a = np.round(a * 31.0) / 31.0
        tiles.append((name, col, a))
    for name, lines in TEXTS:
        col, a = set_text(lines)
        tiles.append((name, col, a))

    # Shelf packing, tallest first.
    order = sorted(range(len(tiles)), key=lambda i: -tiles[i][2].shape[0])
    placed = {}
    x = y = shelf_h = 0
    for i in order:
        h, w = tiles[i][2].shape
        if w + 2 * PAD > ATLAS_W:
            raise SystemExit(f"{tiles[i][0]} is wider than the atlas")
        if x + w + 2 * PAD > ATLAS_W:
            x = 0
            y += shelf_h
            shelf_h = 0
        placed[i] = (x + PAD, y + PAD)
        x += w + 2 * PAD
        shelf_h = max(shelf_h, h + 2 * PAD)
    atlas_h = y + shelf_h
    atlas_h = (atlas_h + 3) & ~3

    rgba = np.zeros((atlas_h, ATLAS_W, 4), np.float32)
    rgba[..., :3] = 1.0
    for i, (name, col, a) in enumerate(tiles):
        px, py = placed[i]
        h, w = a.shape
        rgba[py:py + h, px:px + w, :3] = col
        rgba[py:py + h, px:px + w, 3] = a
    q = np.clip(np.round(rgba * 255.0), 0, 255).astype(np.uint8)
    # Fully transparent texels all look alike, which is what makes the runs long.
    q[q[..., 3] == 0] = (0, 0, 0, 0)

    with open(OUT, "wb") as f:
        f.write(b"MRT1")
        f.write(struct.pack("<II", ATLAS_W, atlas_h))
        for row in q:
            runs = []
            start = 0
            for xx in range(1, ATLAS_W + 1):
                if xx == ATLAS_W or not np.array_equal(row[xx], row[start]) or xx - start == 65535:
                    runs.append((xx - start, row[start]))
                    start = xx
            f.write(struct.pack("<H", len(runs)))
            for n, px in runs:
                f.write(struct.pack("<HBBBB", n, *px.tolist()))
    size = os.path.getsize(OUT)

    with open(HDR, "w", newline="\n") as f:
        f.write("/* Generated by tools/make_decals.py - do not edit.\n"
                " * Each decal's rectangle in the atlas, as u0, v0, u1, v1 with v down,\n"
                " * and its aspect ratio (width / height). */\n")
        f.write("#ifndef MR_DECALS_ATLAS_H\n#define MR_DECALS_ATLAS_H\n")
        f.write(f"#define DECAL_ATLAS_W {ATLAS_W}\n#define DECAL_ATLAS_H {atlas_h}\n")
        for i, (name, col, a) in enumerate(tiles):
            px, py = placed[i]
            h, w = a.shape
            f.write(f"#define DECAL_{name} {px / ATLAS_W:.6f}f, {py / atlas_h:.6f}f, "
                    f"{(px + w) / ATLAS_W:.6f}f, {(py + h) / atlas_h:.6f}f, {w / h:.5f}f\n")
        f.write("#endif\n")
    prev = (q[..., :3].astype(np.float32) * (q[..., 3:] / 255.0) +
            255.0 * (1.0 - q[..., 3:] / 255.0)).astype(np.uint8)
    Image.fromarray(prev).save(os.path.join(os.environ.get("TEMP", "."), "mriya_decals_preview.png"))
    print(f"atlas {ATLAS_W}x{atlas_h}, {len(tiles)} decals, {size} bytes")


if __name__ == "__main__":
    main()
