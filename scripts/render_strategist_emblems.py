"""Render the STRATAGEM Strategist emblem profile pictures to PNG + TGA.

The canonical source is assets/strategists/*.svg. There's no SVG rasterizer on
this box (no cairosvg/rsvg/inkscape/magick), only Pillow - so this script
reproduces the same simple geometry with Pillow's ImageDraw, drawn at 4x and
downsampled with LANCZOS for clean antialiasing. Outputs 256x256 RGBA PNG (UI)
and 32-bit TGA (the engine's texture format) next to the SVGs.

Run:  py scripts/render_strategist_emblems.py
"""
import math, os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "strategists")
SIZE = 256
SS = 4                      # supersample factor
S = SIZE * SS / 56.0        # design units (56) -> supersampled pixels


def x(v): return v * S


def new_canvas():
    img = Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def disc(d, cx, cy, r, fill):
    d.ellipse([x(cx - r), x(cy - r), x(cx + r), x(cy + r)], fill=fill)


def ring(d, cx, cy, r, color, w):
    d.ellipse([x(cx - r), x(cy - r), x(cx + r), x(cy + r)], outline=color, width=int(x(w)))


def chevrons(d, color, w):
    for ox in (17, 27, 37):
        d.line([(x(ox), x(18)), (x(ox + 10), x(28)), (x(ox), x(38))],
               fill=color, width=int(x(w)), joint="curve")


def rommel(d):
    disc(d, 28, 28, 26, "#A32D2D")
    chevrons(d, "#F7C1C1", 4)


def montgomery(d):
    disc(d, 28, 28, 26, "#185FA5")
    pts = [(28, 13), (41, 18), (41, 29), (39, 38), (34, 44), (28, 47),
           (22, 44), (17, 38), (15, 29), (15, 18)]
    d.polygon([(x(a), x(b)) for a, b in pts], fill="#B5D4F4")
    d.line([(x(28), x(20)), (x(28), x(41))], fill="#185FA5", width=int(x(2.5)))


def eisenhower(d):
    disc(d, 28, 28, 26, "#854F0B")
    for bx, by, h in ((16, 32, 10), (25, 25, 17), (34, 18, 24)):
        d.rounded_rectangle([x(bx), x(by), x(bx + 7), x(by + h)],
                            radius=x(1.5), fill="#FAC775")


def giap(d):
    disc(d, 28, 28, 26, "#0F6E56")
    ring(d, 28, 28, 15, "#9FE1CB", 2.5)
    ring(d, 28, 28, 8, "#9FE1CB", 2.5)
    disc(d, 28, 28, 3.5, "#9FE1CB")


def zhukov(d):
    disc(d, 28, 28, 26, "#5F5E5A")
    bbox = [x(28 - 15), x(28 - 15), x(28 + 15), x(28 + 15)]
    seg, gap = 18, 22       # degrees: dashed ring
    a = 0
    while a < 360:
        d.arc(bbox, a, a + seg, fill="#D3D1C7", width=int(x(2.5)))
        a += seg + gap
    d.line([(x(28), x(22)), (x(28), x(34))], fill="#D3D1C7", width=int(x(2.5)))
    d.line([(x(22), x(28)), (x(34), x(28))], fill="#D3D1C7", width=int(x(2.5)))


EMBLEMS = {"rommel": rommel, "montgomery": montgomery, "eisenhower": eisenhower,
           "giap": giap, "zhukov": zhukov}

for name, fn in EMBLEMS.items():
    img, d = new_canvas()
    fn(d)
    img = img.resize((SIZE, SIZE), Image.LANCZOS)
    png = os.path.join(OUT, name + ".png")
    tga = os.path.join(OUT, name + ".tga")
    img.save(png)
    img.save(tga)
    print("wrote", os.path.relpath(png), "+", os.path.basename(tga))
print("done:", len(EMBLEMS), "emblems")
