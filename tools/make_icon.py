#!/usr/bin/env python3
"""Generates res/win32/mriya.ico, the screensaver's icon.

    python tools/make_icon.py

Two drawings, because what reads at 256 pixels is mush at 16:

  48 px and up   the aircraft as the screensaver itself draws it - a render,
                 res/win32/icon_render.png, made with
                     Mriya.scr --window 1024x1024 --dump icon_render.png
                         --frames 60 --weather golden-sea --seed 7 --hud 0
                         --quality 100 --view -66 30 -72 141 -18 60
                 cropped square round the aircraft, in a rounded tile.
  16 - 32 px     the An-225 seen from above - six engines, the twin fins -
                 traced from the packed mesh (build/.../gen/mriya.mesh, so
                 build once first), white on the livery's blue, a yellow edge.

Every size is stored as a PNG inside the .ico (Windows Vista and later).
Needs numpy and Pillow.
"""
import glob
import io
import os
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
OUT = os.path.join(ROOT, "res", "win32", "mriya.ico")
RENDER = os.path.join(ROOT, "res", "win32", "icon_render.png")

BIG = [256, 128, 64, 48]
SMALL = [32, 24, 16]
BLUE = (0, 87, 183)
DEEP = (0, 46, 110)
YELLOW = (255, 206, 0)


def rounded_mask(n, radius):
    """A rounded square, anti-aliased by drawing it large and shrinking."""
    k = 4
    m = Image.new("L", (n * k, n * k), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, n * k - 1, n * k - 1), radius=radius * k, fill=255)
    return m.resize((n, n), Image.LANCZOS)


def big_icon(n, render):
    """The render, cropped square round the aircraft, in a rounded tile."""
    w, h = render.size
    # the aircraft spans x 120..890, y 340..640 of the 1024 render
    side = 860
    cx, cy = 495, 480
    crop = render.crop((cx - side // 2, cy - side // 2, cx + side // 2, cy + side // 2))
    img = crop.resize((n, n), Image.LANCZOS).convert("RGBA")
    if n <= 64:
        img = img.filter(ImageFilter.UnsharpMask(radius=0.8, percent=60, threshold=1))
    img.putalpha(rounded_mask(n, max(2, int(n * 0.18))))
    return img


def load_mesh():
    paths = glob.glob(os.path.join(ROOT, "build", "*", "gen", "mriya.mesh"))
    if not paths:
        raise SystemExit("build the screensaver once first: the small icons are traced from gen/mriya.mesh")
    f = open(paths[0], "rb").read()
    nv, ni = struct.unpack("<II", f[4:12])
    center = np.frombuffer(f[12:24], np.float32)
    half = np.frombuffer(f[24:36], np.float32)
    v = np.frombuffer(f[36:36 + nv * 12], dtype=np.dtype([("p", "<i2", 4), ("n", "i1", 4)]))
    pos = center + v["p"][:, :3].astype(np.float32) / 32767.0 * half
    idx = np.frombuffer(f[36 + nv * 12:36 + nv * 12 + ni * 4], np.uint32).reshape(-1, 3)
    return pos, idx


def planform(n, pos, idx):
    """The aircraft from above, nose up, as a white shape on the tile."""
    k = 8                                   # draw big, shrink: smooth edges
    size = n * k
    span = 2 * 44.4                         # wingtip to wingtip, metres
    scale = size * 0.86 / span
    ox, oz = size * 0.5, size * 0.5 - 1.5 * scale     # the aircraft's middle
    sil = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(sil)
    tri = pos[idx]                          # (t, 3, 3)
    xs = ox + tri[:, :, 0] * scale
    ys = oz + tri[:, :, 2] * scale          # z aft is down the tile: nose up
    for t in range(len(tri)):
        d.polygon([(xs[t, 0], ys[t, 0]), (xs[t, 1], ys[t, 1]), (xs[t, 2], ys[t, 2])], fill=255)
    sil = sil.resize((n, n), Image.LANCZOS)
    # the tile: blue, deeper at the bottom, a yellow edge along the foot
    tile = Image.new("RGBA", (n, n))
    px = tile.load()
    for y in range(n):
        t = y / max(n - 1, 1)
        c = tuple(int(BLUE[i] * (1 - t) + DEEP[i] * t) for i in range(3))
        for x in range(n):
            px[x, y] = c + (255,)
    edge = max(1, round(n * 0.1))
    ImageDraw.Draw(tile).rectangle((0, n - edge, n, n), fill=YELLOW + (255,))
    white = Image.new("RGBA", (n, n), (255, 255, 255, 255))
    tile = Image.composite(white, tile, sil)
    tile.putalpha(rounded_mask(n, max(2, int(n * 0.2))))
    return tile


def write_ico(images, path):
    """An .ico whose entries are all PNG."""
    blobs = []
    for img in images:
        b = io.BytesIO()
        img.save(b, "PNG", optimize=True)
        blobs.append(b.getvalue())
    out = io.BytesIO()
    out.write(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for img, data in zip(images, blobs):
        n = img.size[0]
        out.write(struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(data), offset))
        offset += len(data)
    for data in blobs:
        out.write(data)
    open(path, "wb").write(out.getvalue())


def main():
    render = Image.open(RENDER).convert("RGB")
    pos, idx = load_mesh()
    images = [big_icon(n, render) for n in BIG] + [planform(n, pos, idx) for n in SMALL]
    write_ico(images, OUT)
    # a sheet of every size, to look at
    sheet = Image.new("RGBA", (sum(i.size[0] for i in images) + 10 * len(images), 256), (40, 40, 40, 255))
    x = 0
    for img in images:
        sheet.alpha_composite(img, (x, 256 - img.size[1]))
        x += img.size[0] + 10
    sheet.save(os.path.join(os.environ.get("TEMP", "."), "mriya_icon_sheet.png"))
    print(f"{OUT}: {', '.join(str(i.size[0]) for i in images)} px, {os.path.getsize(OUT)} bytes")


if __name__ == "__main__":
    main()
