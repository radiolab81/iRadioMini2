#!/usr/bin/env python3
"""PNG -> LVGL-9-Rohbild (RGB565 bzw. RGB565A8) fuer die Einbettung per EMBED_FILES.

Aufruf:  tools/png2lvgl.py <name>=<datei.png> ...   (schreibt assets/<name>.bin und assets/assets_dims.h)
Format:  RGB565 little endian (w*h*2 Byte); bei echter Transparenz anschliessend eine A8-Ebene (w*h Byte).
Die Bilder werden direkt aus dem Flash gelesen (kein Dekodieren, kein RAM-Bedarf).
"""
import struct, sys, os
from PIL import Image

out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "components", "irm_disp_lvgl", "assets")
dims = []
for arg in sys.argv[1:]:
    name, path = arg.split("=", 1)
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    px = im.load()
    has_alpha = any(px[x, y][3] < 255 for y in range(h) for x in range(w))
    color, alpha = bytearray(), bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            color += struct.pack("<H", ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
            alpha.append(a)
    data = bytes(color) + (bytes(alpha) if has_alpha else b"")
    with open(os.path.join(out_dir, name + ".bin"), "wb") as f:
        f.write(data)
    dims.append((name.upper(), w, h, has_alpha))
    print(f"{name}: {w}x{h} {'RGB565A8' if has_alpha else 'RGB565'} {len(data)} Byte")

with open(os.path.join(out_dir, "assets_dims.h"), "w") as f:
    f.write("/* erzeugt von tools/png2lvgl.py - nicht von Hand aendern */\n#pragma once\n")
    for n, w, h, a in dims:
        f.write(f"#define IRM_ASSET_{n}_W {w}\n#define IRM_ASSET_{n}_H {h}\n#define IRM_ASSET_{n}_ALPHA {1 if a else 0}\n")
