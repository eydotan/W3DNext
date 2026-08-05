#!/usr/bin/env python3
"""Rasterize the HTML wallpaper designs in assets/wallpapers/src/*.html to PNG.

Each HTML page is a fixed 2048x1024 canvas; this renders it with headless
Chrome to assets/wallpapers/<name>.png, where it joins the splash rotation.

    py scripts/render_wallpapers.py [name ...]

With no args, renders every src/*.html. With args, only those stems.
"""

import glob
import os
import subprocess
import sys

W, H = 2048, 1024
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
SRC = os.path.join(ROOT, "assets", "wallpapers", "src")
OUT = os.path.join(ROOT, "assets", "wallpapers")

CHROME_CANDIDATES = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
]


def find_chrome():
    for c in CHROME_CANDIDATES:
        if os.path.exists(c):
            return c
    raise SystemExit("[render] no Chrome/Edge found; edit CHROME_CANDIDATES")


def render(chrome, html, png):
    url = "file:///" + html.replace("\\", "/").replace(" ", "%20")
    cmd = [
        chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars",
        "--force-device-scale-factor=1", f"--window-size={W},{H}",
        "--default-background-color=ff000000", f"--screenshot={png}", url,
    ]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    chrome = find_chrome()
    wanted = set(sys.argv[1:])
    n = 0
    for html in sorted(glob.glob(os.path.join(SRC, "*.html"))):
        stem = os.path.splitext(os.path.basename(html))[0]
        if wanted and stem not in wanted:
            continue
        png = os.path.join(OUT, stem + ".png")
        render(chrome, html, png)
        ok = os.path.exists(png)
        print(f"[render] {stem}.html -> {stem}.png {'OK' if ok else 'FAILED'}")
        n += ok
    print(f"[render] {n} wallpaper(s) rendered to {OUT}")


if __name__ == "__main__":
    main()
