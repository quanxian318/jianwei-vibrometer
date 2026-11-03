"""Reproducible vector speckles in millimetres; no repeating tile or gray dots."""
from pathlib import Path
import hashlib
import json
import math
import random

import numpy as np
from PIL import Image
from pypdf import PdfReader
import pypdfium2 as pdfium
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas
from reportlab.lib.units import mm

OUT = Path(__file__).resolve().parent
FIELD_W, FIELD_H = 190.0, 260.0
X0, Y0 = 10.0, 16.0
VARIANTS = [("细", "fine", .5, 2026102101),
            ("中", "medium", .8, 2026102102),
            ("粗", "coarse", 1.2, 2026102103)]


def pattern(diameter, seed):
    rng = random.Random(seed)
    cell = diameter * 1.2
    buckets = {}
    circles = []
    area = 0.0
    attempts = 0
    goal = FIELD_W * FIELD_H * .42
    while area < goal:
        attempts += 1
        if attempts > 30_000_000:
            raise RuntimeError("Packing did not reach specified coverage")
        r = diameter * rng.uniform(.425, .575)
        x = rng.uniform(r, FIELD_W - r)
        y = rng.uniform(r, FIELD_H - r)
        ix, iy = math.floor(x / cell), math.floor(y / cell)
        valid = True
        for bx in range(ix - 1, ix + 2):
            for by in range(iy - 1, iy + 2):
                for px, py, pr in buckets.get((bx, by), ()):
                    if (x - px) ** 2 + (y - py) ** 2 < (r + pr + diameter * .025) ** 2:
                        valid = False
                        break
                if not valid:
                    break
            if not valid:
                break
        if not valid:
            continue
        circle = (x, y, r)
        circles.append(circle)
        buckets.setdefault((ix, iy), []).append(circle)
        area += math.pi * r * r
    return circles, area / (FIELD_W * FIELD_H)


def pdf_page(c, label, diameter, circles):
    c.setPageSize((210 * mm, 297 * mm))
    c.setFillColorRGB(0, 0, 0)
    c.setStrokeColorRGB(0, 0, 0)
    c.setFont("Kai", 10)
    c.drawString(10 * mm, 284 * mm, f"视觉测振 · 随机散斑 / {label}档 / 标称点径 {diameter:g} mm")
    c.setFont("Kai", 8)
    c.drawRightString(200 * mm, 284 * mm, "A4 · 实际大小 100%")
    # The field has no text, frame, fiducials or repeated tiles.
    c.saveState()
    c.translate(X0 * mm, Y0 * mm)
    c.scale(mm, mm)
    for start in range(0, len(circles), 512):
        path = c.beginPath()
        for x, y, r in circles[start:start + 512]:
            path.circle(x, y, r)
        c.drawPath(path, fill=1, stroke=0)
    c.restoreState()
    c.setLineWidth(.2 * mm)
    c.line(10 * mm, 9 * mm, 60 * mm, 9 * mm)
    for x in range(10, 61):
        tick = 2 if (x - 10) % 10 == 0 else 1
        c.line(x * mm, 9 * mm, x * mm, (9 + tick) * mm)
    c.setFont("Kai", 7)
    c.drawString(64 * mm, 8.5 * mm, "标尺 50 mm；图案区 190×260 mm；仅裁剪图案区粘贴")
    c.showPage()


def svg_page(name, diameter, circles):
    # Vector original, matching the PDF's physical dot positions.
    rows = ['<svg xmlns="http://www.w3.org/2000/svg" width="210mm" height="297mm" viewBox="0 0 210 297">',
            '<rect width="210" height="297" fill="white"/>',
            f'<text x="10" y="13" font-family="sans-serif" font-size="3">RANDOM SPECKLE | {name.upper()} | {diameter:g} mm | PRINT 100%</text>',
            '<g fill="black">']
    for x, y, r in circles:
        rows.append(f'<circle cx="{X0+x:.5f}" cy="{297-Y0-y:.5f}" r="{r:.5f}"/>')
    rows.extend(['</g>', '<path d="M10 288 H60 M10 286 V290 M60 286 V290" fill="none" stroke="black" stroke-width="0.2"/>',
                 '<text x="64" y="289" font-family="sans-serif" font-size="2.6">50 mm | PATTERN 190 x 260 mm</text>', '</svg>'])
    (OUT / f"散斑_A4_{name}_{diameter:g}mm.svg").write_text("\n".join(rows), encoding="utf-8")


def coarse_sheet():
    diameter, seed = 2.0, 2026102104
    circles, coverage = pattern(diameter, seed)
    path = OUT / "asset_6ad18eb660.pdf"
    c = canvas.Canvas(str(path), pagesize=(210 * mm, 297 * mm), pageCompression=1)
    c.setTitle("A4 随机散斑 · 2 mm 粗点打印版")
    pdf_page(c, "粗点", diameter, circles)
    c.save()
    svg_page("large", diameter, circles)
    r = PdfReader(path)
    assert len(r.pages) == 1 and abs(float(r.pages[0].mediabox.width)/mm-210) < .001
    doc = pdfium.PdfDocument(path)
    im = doc[0].render(scale=600/72, no_smoothpath=True).to_pil().convert("L")
    mono = im.point(lambda p: 255 if p >= 128 else 0, mode="1")
    mono.save(OUT / "asset_6076f9386a.png", dpi=(600,600), optimize=True)
    ppm = 600 / 25.4
    crop = np.asarray(mono.crop((round(10*ppm),round(21*ppm),round(200*ppm),round(281*ppm))))
    fraction = float(1-crop.mean())
    assert .40 < fraction < .44
    preview = im.copy(); preview.thumbnail((900,1273))
    preview.save(OUT / "asset_68b1df2da3.png")
    return dict(name="large", nominal_diameter_mm=diameter, diameter_range_mm=[1.7,2.3],
                dots=len(circles), vector_black_fraction=coverage, seed=seed,
                raster_black_fraction_600dpi=fraction, separate_pdf=path.name)


def main():
    pdfmetrics.registerFont(TTFont("Kai", "C:/Windows/Fonts/simkai.ttf"))
    pdf_path = OUT / "asset_a74faa9be8.pdf"
    c = canvas.Canvas(str(pdf_path), pagesize=(210 * mm, 297 * mm), pageCompression=1)
    c.setTitle("视觉测振仪 · A4 黑白随机散斑（0.5 / 0.8 / 1.2 mm）")
    c.setAuthor("见微视觉测振仪")
    stats = []
    for label, name, diameter, seed in VARIANTS:
        circles, coverage = pattern(diameter, seed)
        pdf_page(c, label, diameter, circles)
        svg_page(name, diameter, circles)
        stats.append(dict(name=name, nominal_diameter_mm=diameter,
                          diameter_range_mm=[diameter * .85, diameter * 1.15],
                          dots=len(circles), vector_black_fraction=coverage, seed=seed))
        print(f"{name}: {len(circles)} dots, black={coverage:.4%}", flush=True)
    c.save()
    reader = PdfReader(pdf_path)
    assert len(reader.pages) == 3
    render = pdfium.PdfDocument(pdf_path)
    previews = []
    for i, (label, name, diameter, _) in enumerate(VARIANTS):
        page = reader.pages[i]
        assert abs(float(page.mediabox.width) / mm - 210) < .001
        assert abs(float(page.mediabox.height) / mm - 297) < .001
        assert label + "档" in page.extract_text()
        # Raster is rendered from the PDF, not independently redrawn.
        pil = render[i].render(scale=600 / 72, no_smoothpath=True).to_pil().convert("L")
        mono = pil.point(lambda p: 255 if p >= 128 else 0, mode="1")
        png_path = OUT / f"散斑_A4_{name}_{diameter:g}mm_600dpi.png"
        mono.save(png_path, dpi=(600, 600), optimize=True)
        pxmm = 600 / 25.4
        region = np.asarray(mono.crop((round(X0 * pxmm), round((297-Y0-FIELD_H) * pxmm),
                                       round((X0+FIELD_W) * pxmm), round((297-Y0) * pxmm))))
        stats[i]["raster_black_fraction_600dpi"] = float(1 - region.mean())
        assert .40 < stats[i]["raster_black_fraction_600dpi"] < .44
        assert mono.size[0] in (4961, 4962) and mono.size[1] in (7016, 7017)
        preview = pil.copy()
        preview.thumbnail((700, 990))
        previews.append(preview.convert("RGB"))
    contact = Image.new("RGB", (sum(p.width for p in previews), max(p.height for p in previews)), "white")
    x = 0
    for p in previews:
        contact.paste(p, (x, 0)); x += p.width
    contact.save(OUT / "asset_48d60db71a.png")
    manifest = dict(page_mm=[210,297], pattern_mm=[FIELD_W,FIELD_H], png_dpi=600,
                    pdf_pages=3, raster_binary=True, periodic_tile=False, patterns=stats)
    manifest["patterns"].append(coarse_sheet())
    manifest["sha256"] = {p.name:hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in sorted(OUT.iterdir()) if p.suffix in (".pdf", ".svg", ".png")}
    (OUT / "asset_de93f3cf92.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print("PDF/page dimensions, Chinese labels and raster coverage checked.", flush=True)


if __name__ == "__main__":
    main()
