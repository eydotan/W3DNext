#!/usr/bin/env python3
"""frame_oracle - the framedump measurement kit for the D3D11 parity work.

Subcommands
  mae A B [--crop L,T,R,B]
      Whole-frame (or cropped) mean-absolute-error between two dumps.

  motion DIR PREFIX --fa N --fb N --region name,L,T,R,B [...] --control L,T,R,B
      Adjacent-frame motion oracle (the Spooky method, 2026-07-28): animation
      shows as a high crop-MAE between two closely-spaced frames of the SAME
      run, normalized by a static control region (compression dither, cloud
      pass). ratio = region_delta / max(control_delta, 0.05). Frozen
      animation reads ~1, live animation reads >~2. Single-frame parity
      is structurally blind to this defect class (a frozen tree matches its
      own first frame forever) - hence this oracle.

  magenta DIR [DIR...]
      Anomaly scan over every .ppm/.png: PURE-FALLBACK magenta (the backend's
      missing-format checker, r,b>200 g<60) vs dim-overlay magenta (saturated
      at any brightness - dev toasts, r,b>120, g+60<r,b), plus near-black
      full frames. Two classes because the first single-class rule missed the
      ~55%-brightness -stratagemShot toast (parity log, 2026-07-28).

  selftest
      Fixture negative control: synthetic frames that MUST fire each verdict
      (moving region fires motion, frozen must not; planted magenta patches
      fire their class; identical frames read mae 0). Exit 1 on any miss -
      run this before trusting any other subcommand's GREEN.
"""
import argparse
import glob
import os
import sys

from PIL import Image, ImageChops


def mae(a, b):
    d = ImageChops.difference(a, b)
    h = d.histogram()
    return sum(v * h[band * 256 + v] for band in range(3) for v in range(256)) / (
        a.size[0] * a.size[1] * 3)


def load(path):
    return Image.open(path).convert("RGB")


# Named crop presets are FRAMING-ANCHORED: a pixel rect only measures what it
# claims while the camera framing that defined it holds, and that framing is a
# function of resolution. Spooky's step-8 probe read the raw terrain rect at a
# different resolution and it landed on a building - a would-be false 2.4x
# regression (parity log 2026-07-28). Each preset therefore pins the exact
# dump size it was calibrated on and the command REFUSES other sizes instead
# of silently measuring different scene content.
CROP_PRESETS = {
    # name: (required_size, box, what the rect actually contains)
    "terrain": ((1280, 720), (540, 330, 940, 470),
                "open terrain left of the supply dock, -stratagemShot f2700"),
}


def cmd_mae(args):
    a, b = load(args.a), load(args.b)
    if args.preset:
        size, box, desc = CROP_PRESETS[args.preset]
        if a.size != size or b.size != size:
            print(f"REFUSED: preset '{args.preset}' is calibrated for {size[0]}x{size[1]} "
                  f"dumps ({desc}); got {a.size[0]}x{a.size[1]} / {b.size[0]}x{b.size[1]}. "
                  f"A pixel rect at a different framing measures different scene content - "
                  f"re-anchor by eye and pass --crop explicitly.")
            return 2
        a, b = a.crop(box), b.crop(box)
    elif args.crop:
        box = tuple(int(x) for x in args.crop.split(","))
        a, b = a.crop(box), b.crop(box)
    print(f"mae={mae(a, b):.2f}")
    return 0


def motion_ratios(fa_img, fb_img, regions, control):
    ctrl = max(mae(fa_img.crop(control), fb_img.crop(control)), 0.05)
    out = {}
    for name, box in regions:
        delta = mae(fa_img.crop(box), fb_img.crop(box))
        out[name] = (delta, delta / ctrl)
    return ctrl, out


def cmd_motion(args):
    fa = load(os.path.join(args.dir, f"{args.prefix}_f{args.fa}.ppm"))
    fb = load(os.path.join(args.dir, f"{args.prefix}_f{args.fb}.ppm"))
    regions = []
    for spec in args.region:
        name, l, t, r, b = spec.split(",")
        regions.append((name, (int(l), int(t), int(r), int(b))))
    control = tuple(int(x) for x in args.control.split(","))
    ctrl, out = motion_ratios(fa, fb, regions, control)
    print(f"{args.prefix} f{args.fa}->f{args.fb} control_delta={ctrl:.2f}")
    worst_fail = None
    for name, (delta, ratio) in out.items():
        verdict = "MOVING" if ratio >= args.min_ratio else "FROZEN"
        print(f"  {name}: delta={delta:.2f} ratio={ratio:.2f} {verdict}")
        if ratio < args.min_ratio:
            worst_fail = name
    if args.expect == "moving" and worst_fail is not None:
        print(f"MOTION FAIL: {worst_fail} below ratio {args.min_ratio}")
        return 1
    return 0


def scan_file(fp):
    img = load(fp)
    px = img.load()
    w, h = img.size
    pure = dim = 0
    lum_total = 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            r, g, b = px[x, y]
            if r > 200 and b > 200 and g < 60:
                pure += 1
            elif r > 120 and b > 120 and g + 60 < r and g + 60 < b:
                dim += 1
            lum_total += r + g + b
    samples = len(range(0, h, 2)) * len(range(0, w, 2))
    return pure, dim, lum_total / samples / 3


def cmd_magenta(args):
    total = flagged = 0
    for d in args.dirs:
        files = sorted(glob.glob(os.path.join(d, "**", "*.ppm"), recursive=True))
        files += sorted(glob.glob(os.path.join(d, "**", "*.png"), recursive=True))
        for fp in files:
            total += 1
            pure, dim, lum = scan_file(fp)
            if pure > 2 or dim > 20 or lum < 2.0:
                flagged += 1
                kind = ("PURE-FALLBACK magenta=%d" % pure if pure > 2 else
                        "dim-overlay magenta=%d" % dim if dim > 20 else
                        "near-black meanlum=%.2f" % lum)
                print(f"ANOMALY {fp}: {kind}")
    print(f"scanned {total} frames, {flagged} anomalies")
    return 0


def cmd_selftest(_args):
    import random
    rng = random.Random(1234)
    w, h = 320, 200
    base = Image.new("RGB", (w, h))
    px = base.load()
    for y in range(h):
        for x in range(w):
            v = rng.randrange(40, 200)
            px[x, y] = (v, v, v)
    moved = base.copy()
    mpx = moved.load()
    for y in range(20, 80):          # "tree" region (10,20,90,80) shifts
        for x in range(10, 88):
            mpx[x + 2, y] = px[x, y]
    fails = []
    # motion: moved region must read MOVING, untouched region FROZEN
    ctrl, out = motion_ratios(base, moved,
        [("moving", (10, 20, 90, 80)), ("frozen", (200, 100, 300, 180))],
        (120, 120, 190, 190))
    if out["moving"][1] < 2.0:
        fails.append(f"motion: moving region ratio {out['moving'][1]:.2f} < 2.0")
    if out["frozen"][1] > 1.5:
        fails.append(f"motion: frozen region ratio {out['frozen'][1]:.2f} > 1.5")
    # mae identity
    if mae(base, base) != 0.0:
        fails.append("mae: identical images read nonzero")
    # magenta classes: plant pure + dim patches
    mag = base.copy()
    gpx = mag.load()
    for y in range(0, 10):
        for x in range(0, 10):
            gpx[x, y] = (255, 0, 255)          # pure fallback
            gpx[x + 20, y] = (139, 11, 143)    # the dim toast color that was missed
    p, d, _ = scan_file_img(mag)
    if p <= 2:
        fails.append("magenta: planted PURE patch not flagged")
    if d <= 2:
        fails.append("magenta: planted dim-toast patch not flagged")
    for f in fails:
        print("SELFTEST FAIL:", f)
    print("SELFTEST", "FAIL" if fails else "PASS")
    return 1 if fails else 0


def scan_file_img(img):
    px = img.load()
    w, h = img.size
    pure = dim = 0
    lum = 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            r, g, b = px[x, y]
            if r > 200 and b > 200 and g < 60:
                pure += 1
            elif r > 120 and b > 120 and g + 60 < r and g + 60 < b:
                dim += 1
            lum += r + g + b
    return pure, dim, lum / (len(range(0, h, 2)) * len(range(0, w, 2))) / 3


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)
    pm = sub.add_parser("mae")
    pm.add_argument("a"); pm.add_argument("b"); pm.add_argument("--crop")
    pm.add_argument("--preset", choices=sorted(CROP_PRESETS))
    pm.set_defaults(fn=cmd_mae)
    po = sub.add_parser("motion")
    po.add_argument("dir"); po.add_argument("prefix")
    po.add_argument("--fa", type=int, required=True)
    po.add_argument("--fb", type=int, required=True)
    po.add_argument("--region", action="append", required=True)
    po.add_argument("--control", required=True)
    po.add_argument("--min-ratio", type=float, default=2.0)
    po.add_argument("--expect", choices=["moving", "report"], default="report")
    po.set_defaults(fn=cmd_motion)
    pg = sub.add_parser("magenta")
    pg.add_argument("dirs", nargs="+")
    pg.set_defaults(fn=cmd_magenta)
    ps = sub.add_parser("selftest")
    ps.set_defaults(fn=cmd_selftest)
    args = p.parse_args()
    sys.exit(args.fn(args))


if __name__ == "__main__":
    main()
