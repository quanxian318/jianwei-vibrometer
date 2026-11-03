#!/usr/bin/env python3
"""Launcher icons for the Android app from the brand mark (same geometry as
Logo() in web/js/icons.js): adaptive layers (background, foreground,
monochrome), legacy square and round icons per density, and a 512 px store
icon in docs. Needs Pillow.   python3 tools/make_icons.py
"""
import pathlib

from PIL import Image, ImageDraw

root = pathlib.Path(__file__).resolve().parents[1]
res = root / 'android' / 'res'
SS = 8  # supersampling


def bezier(p0, p1, p2, p3, n=24):
    out = []
    for i in range(n + 1):
        t = i / n
        a = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t * t, t ** 3
        out.append((a[0] * p0[0] + a[1] * p1[0] + a[2] * p2[0] + a[3] * p3[0], a[0] * p0[1] + a[1] * p1[1] + a[2] * p2[1] + a[3] * p3[1]))
    return out


# The wave of the mark on the 108-unit grid (the SVG path of Logo()).
WAVE = []
for seg in [((14, 54), (20, 54), (22, 40), (28, 40)), ((28, 40), (34, 40), (36, 68), (42, 68)), ((42, 68), (48, 68), (48, 38), (54, 38)),
            ((54, 38), (60, 38), (60, 68), (66, 68)), ((66, 68), (72, 68), (74, 40), (80, 40)), ((80, 40), (86, 40), (88, 54), (94, 54))]:
    WAVE += bezier(*seg)


def gradient(size, c0, c1):
    g = Image.new('RGBA', (size, size))
    px = g.load()
    for y in range(size):
        for x in range(size):
            t = (x + y) / (2 * (size - 1))
            px[x, y] = tuple(int(c0[i] + (c1[i] - c0[i]) * t) for i in range(3)) + (255,)
    return g


def mark(size, scale=1.0, mono=False):
    """Ring, inner ring, wave and dot on a transparent layer (108-unit grid)."""
    S = size * SS
    k = S / 108 * scale
    off = S / 2 - 54 * k
    P = lambda x, y: (off + x * k, off + y * k)
    layer = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    ring = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(ring)
    c = P(54, 54)
    d.ellipse([c[0] - 29 * k, c[1] - 29 * k, c[0] + 29 * k, c[1] + 29 * k], outline=255, width=max(1, int(7 * k)))
    inner = Image.new('L', (S, S), 0)
    ImageDraw.Draw(inner).ellipse([c[0] - 16 * k, c[1] - 16 * k, c[0] + 16 * k, c[1] + 16 * k], outline=115, width=max(1, int(2.5 * k)))
    if mono:
        white = Image.new('RGBA', (S, S), (255, 255, 255, 255))
        layer.paste(white, (0, 0), ring)
        layer.paste(white, (0, 0), inner)
    else:
        g = gradient(S, (91, 124, 255), (162, 123, 255))
        layer.paste(g, (0, 0), ring)
        layer.paste(g, (0, 0), inner)
    wd = ImageDraw.Draw(layer)
    pts = [P(x, y) for x, y in WAVE]
    w = int(5 * k)
    wd.line(pts, fill=(255, 255, 255, 255), width=w, joint='curve')
    for e in (pts[0], pts[-1]):
        wd.ellipse([e[0] - w / 2, e[1] - w / 2, e[0] + w / 2, e[1] + w / 2], fill=(255, 255, 255, 255))
    wd.ellipse([c[0] - 4 * k, c[1] - 4 * k, c[0] + 4 * k, c[1] + 4 * k], fill=(255, 255, 255, 255))
    return layer


def background(size):
    S = size * SS
    bg = gradient(S, (27, 33, 64), (10, 13, 26))
    # Soft light from the top left.
    glow = Image.new('L', (S, S), 0)
    ImageDraw.Draw(glow).ellipse([-S * 0.3, -S * 0.45, S * 0.9, S * 0.55], fill=38)
    from PIL import ImageFilter
    glow = glow.filter(ImageFilter.GaussianBlur(S * 0.12))
    bg.paste(Image.new('RGBA', (S, S), (120, 140, 255, 255)), (0, 0), glow)
    return bg


def down(img, size):
    return img.resize((size, size), Image.LANCZOS)


def legacy(size, round_):
    S = size * SS
    base = background(size)
    base.alpha_composite(mark(size, 1.0))
    mask = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(mask)
    pad = S * 0.04
    if round_:
        d.ellipse([pad, pad, S - pad, S - pad], fill=255)
    else:
        d.rounded_rectangle([pad, pad, S - pad, S - pad], radius=S * 0.24, fill=255)
    out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    out.paste(base, (0, 0), mask)
    return down(out, size)


DENS = {'mdpi': 1, 'hdpi': 1.5, 'xhdpi': 2, 'xxhdpi': 3, 'xxxhdpi': 4}
for name, f in DENS.items():
    d = res / ('mipmap-' + name)
    d.mkdir(parents=True, exist_ok=True)
    a, l = int(108 * f), int(48 * f)
    down(background(a), a).save(d / 'ic_launcher_background.png')
    down(mark(a, 0.72), a).save(d / 'ic_launcher_foreground.png')  # inside the 66 dp safe zone
    down(mark(a, 0.72, mono=True), a).save(d / 'ic_launcher_monochrome.png')
    legacy(l, False).save(d / 'ic_launcher.png')
    legacy(l, True).save(d / 'ic_launcher_round.png')
docs = root / 'docs'
docs.mkdir(exist_ok=True)
store = background(512)
store.alpha_composite(mark(512, 0.9))
down(store, 512).convert('RGB').save(docs / 'icon_512.png')
legacy(192, False).save(docs / 'icon_rounded_192.png')
print('icons written to', res)
