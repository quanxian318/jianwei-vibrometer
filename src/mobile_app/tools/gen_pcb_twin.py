#!/usr/bin/env python3
"""Board textures and 3D part lists for the app's digital twin, from the real
PCB fabrication files in this repository.

For each board (main carrier, front panel) it reads the Gerber zip (top
copper, solder mask, silkscreen, outline, drills), FlyingProbeTesting.json
(component positions and rotations) and the BOM (packages), and writes

  web/img/pcb_<name>.webp   top view, 1 px = 1/PX_MM mm, image y downwards
  web/js/pcb_<name>.js      window.PCB_BOARDS.<name> = {w, h, tex, parts}

Low parts (chip resistors/capacitors, SOT/SOD, diodes, fuses) are painted into
the texture; tall ones (connectors, electrolytics, inductors, ICs, headers)
become 3D boxes/cylinders in board millimetres, x right, y down, h upwards.

Developer tool, outputs are committed. Needs: pip install gerbonara cairosvg pillow
  python3 tools/gen_pcb_twin.py
"""
import csv
import io
import json
import math
import os
import sys
import tempfile
import zipfile

import cairosvg
from gerbonara import ExcellonFile, GerberFile
from gerbonara.utils import MM
from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(HERE)
ROOT = os.path.abspath(os.path.join(APP, '..', '..'))
HW = os.path.join(ROOT, '01_硬件工程')
DESIGN = HW
PX_MM = 14  # texture resolution

BOARDS = {
    'main': dict(zip='carrier_gerber.zip', bom='carrier_bom.csv',
                 bodies=os.path.join('丝印', '主载板', '模块导入尺寸与坐标.json')),
    'front': dict(zip='front_panel_gerber.zip', bom='front_panel_bom.csv', bodies=None),
}

# Package -> painted body (length along x at 0 deg, width, colour, kind).
FLAT = {
    '0603': (1.6, 0.8), '0805': (2.0, 1.25), '1206': (3.2, 1.6), '1812': (4.5, 3.2), '2512': (6.3, 3.2),
    '0603x4': (3.2, 1.6), 'SOD-123': (2.7, 1.6), 'SMA': (4.3, 2.6), 'SMB': (4.4, 3.6),
    'SOT-23': (2.9, 1.3), 'SOT-23-5': (2.9, 1.6), 'SOT-23-6': (2.9, 1.6), 'SMD,10.3x3.3mm': (10.3, 3.3),
}
# Package -> 3D body (length, width, height, material, shape).
TALL = {
    'D10xL16mm,P=5mm': (10, 10, 16, 'elcap', 'cyl'),
    'SMD,D6.3xL5.8mm': (6.6, 6.6, 5.8, 'elcap', 'cyl'),
    'SMD,11x10mm': (10, 11, 3.8, 'inductor', 'box'),
    '2917': (7.3, 4.3, 2.8, 'polycap', 'box'),
    'TO-252': (6.6, 6.1, 2.3, 'ic', 'box'),
    'SOIC-8-EP': (4.9, 3.9, 1.6, 'ic', 'box'), 'SOIC-8': (4.9, 3.9, 1.6, 'ic', 'box'),
    'SOIC-16-300mil': (10.3, 7.5, 2.6, 'ic', 'box'), 'SOT-223': (6.5, 3.5, 1.7, 'ic', 'box'),
    'TSSOP-20': (6.5, 4.4, 1.1, 'ic', 'box'), 'TSSOP-24': (7.8, 4.4, 1.1, 'ic', 'box'),
    'VSSOP-10': (3.0, 3.0, 1.0, 'ic', 'box'), 'VSSOP-8': (2.3, 2.0, 0.9, 'ic', 'box'),
    'TQFN-16-EP(3x3)': (3, 3, 0.8, 'ic', 'box'),
    'SMD5050-4P': (5, 5, 1.6, 'led', 'box'), 'SMD-4P,5.1x5.1mm': (5.1, 5.1, 1.5, 'tact', 'box'),
    'SMD': (14, 15, 1.6, 'metal', 'box'),  # J603 microSD (the other 'SMD' part is BT801, below)
}
# Connector heights / materials by part number (outline comes from the body JSON).
CONN = {
    'PJ-002BH': (11, 'jack'), 'JL128-50802G01': (10, 'terminal'), 'DB128V-5.08-6P-GN-S': (10, 'terminal'),
    'U-A-24DD-Y-14': (7, 'usb'), 'S5B-PH-K-S(LF)(SN)': (6, 'housing'), 'DM3AT-SF-PEJM5': (1.6, 'metal'),
    'PM254-1-15-Z-8.5': (8.5, 'socket'), 'DC3-2.54-40PAS': (9, 'shroud'), 'KH-BS1220-2-SMT': (4, 'coin'),
    'PZ254V-11-03P': (8.5, 'pins'), 'PZ254V-11-04P': (8.5, 'pins'),
}


def mil(v):
    return v * 0.0254


def board_size(gko):
    (x0, y0), (x1, y1) = gko.bounding_box(MM)
    # The outline stroke is centred on the edge.
    return round(x1 - x0 - 0.254, 3), round(y1 - y0 - 0.254, 3), (x0 + 0.127, y0 + 0.127)


def raster(obj, w, h, org, px):
    """Layer as an 8-bit mask (255 = drawn), image y downwards."""
    svg = str(obj.to_svg(force_bounds=((org[0], org[1]), (org[0] + w, org[1] + h)), fg='white', bg='black'))
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=round(w * px), output_height=round(h * px))
    im = Image.open(io.BytesIO(png)).convert('L')
    return im


def solid(size, col):
    return Image.new('RGB', size, col)


def comp(base, mask, col, alpha=1.0):
    m = mask if alpha >= 1 else mask.point(lambda v: int(v * alpha))
    base.paste(solid(base.size, col), (0, 0), m)


def rot_rect(cx, cy, l, w, ang):
    a = math.radians(ang)
    c, s = math.cos(a), math.sin(a)
    return [(cx + x * c - y * s, cy + x * s + y * c) for x, y in ((-l / 2, -w / 2), (l / 2, -w / 2), (l / 2, w / 2), (-l / 2, w / 2))]


def paint_part(dr, pkg, ref, cx, cy, ang, px):
    """Top view of a low part, in pixel coordinates (y down)."""
    l, w = FLAT[pkg]
    P = lambda pts: [(x * px, y * px) for x, y in pts]
    # Contact shadow.
    dr.polygon(P(rot_rect(cx + 0.12, cy + 0.15, l + 0.2, w + 0.2, ang)), fill=(8, 30, 20))
    if pkg in ('0603', '0805', '1206', '0603x4', '2512', '1812'):
        end = min(0.35 * l, 0.5 if pkg != '2512' else 0.9)
        body = (24, 24, 26) if ref.startswith(('R', 'RN', 'RS')) else (172, 132, 86) if ref.startswith('C') else (196, 160, 60) if ref.startswith('F') else (40, 40, 44)
        dr.polygon(P(rot_rect(cx, cy, l, w, ang)), fill=body)
        for sgn in (-1, 1):
            ex = cx + sgn * (l / 2 - end / 2) * math.cos(math.radians(ang))
            ey = cy + sgn * (l / 2 - end / 2) * math.sin(math.radians(ang))
            dr.polygon(P(rot_rect(ex, ey, end, w, ang)), fill=(200, 202, 206))
        if pkg == '0603x4':  # four elements
            for i in range(4):
                o = -l / 2 + l * (i + 0.5) / 4
                ex, ey = cx + o * math.cos(math.radians(ang)), cy + o * math.sin(math.radians(ang))
                dr.polygon(P(rot_rect(ex, ey, 0.4, w * 1.05, ang)), outline=(200, 202, 206))
    else:
        # Moulded packages: dark body, legs, cathode band on diodes.
        legs = {'SOT-23': [(-0.95, 1), (0.95, 1), (0, -1)], 'SOT-23-5': [(-0.95, 1), (0, 1), (0.95, 1), (-0.95, -1), (0.95, -1)],
                'SOT-23-6': [(-0.95, 1), (0, 1), (0.95, 1), (-0.95, -1), (0, -1), (0.95, -1)]}.get(pkg, [])
        a = math.radians(ang)
        for lx, side in legs:
            ly = side * (w / 2 + 0.3)
            dr.polygon(P(rot_rect(cx + lx * math.cos(a) - ly * math.sin(a), cy + lx * math.sin(a) + ly * math.cos(a), 0.42, 0.7, ang)), fill=(196, 198, 202))
        if pkg in ('SMA', 'SMB', 'SOD-123', 'SMD,10.3x3.3mm'):
            for sgn in (-1, 1):
                ex, ey = cx + sgn * (l / 2 + 0.25) * math.cos(a), cy + sgn * (l / 2 + 0.25) * math.sin(a)
                dr.polygon(P(rot_rect(ex, ey, 0.6, w * 0.7, ang)), fill=(196, 198, 202))
        body = (230, 230, 226) if pkg == 'SMD,10.3x3.3mm' else (30, 31, 34)
        dr.polygon(P(rot_rect(cx, cy, l, w, ang)), fill=body)
        dr.polygon(P(rot_rect(cx - 0.12 * math.cos(a), cy - 0.12 * math.sin(a), l * 0.86, w * 0.7, ang)), fill=tuple(min(255, v + 12) for v in body))
        if pkg in ('SMA', 'SMB', 'SOD-123'):
            bx, by = cx - (l / 2 - 0.6) * math.cos(a), cy - (l / 2 - 0.6) * math.sin(a)
            dr.polygon(P(rot_rect(bx, by, 0.5, w, ang)), fill=(150, 152, 158))


def build(name, cfg, out_img, out_js):
    z = zipfile.ZipFile(os.path.join(HW, cfg['zip']))
    tmp = tempfile.mkdtemp()
    z.extractall(tmp)
    G = lambda n: GerberFile.open(os.path.join(tmp, n))
    w, h, org = board_size(G('Gerber_BoardOutlineLayer.GKO'))
    px = PX_MM
    size = (round(w * px), round(h * px))
    outline = raster(G('Gerber_BoardOutlineLayer.GKO'), w, h, org, px)
    # Board area: flood the outside from a corner, invert.
    area = outline.point(lambda v: 255 if v > 60 else 0).convert('L')
    ImageDraw.floodfill(area, (0, 0), 128)
    inside = area.point(lambda v: 0 if v == 128 else 255)
    copper = raster(G('Gerber_TopLayer.GTL'), w, h, org, px)
    mask_open = raster(G('Gerber_TopSolderMaskLayer.GTS'), w, h, org, px)
    silk = raster(G('Gerber_TopSilkscreenLayer.GTO'), w, h, org, px)
    drills = Image.new('L', size, 0)
    for dn in ('Drill_PTH_Through.DRL', 'Drill_PTH_Through_Via.DRL', 'Drill_NPTH_Through.DRL'):
        p = os.path.join(tmp, dn)
        if os.path.exists(p):
            drills = ImageChops.lighter(drills, raster(ExcellonFile.open(p), w, h, org, px))

    # Solder mask over FR-4: darker where there is no copper, lighter (raised,
    # lit) over traces and pours; pads exposed as ENIG gold.
    img = solid(size, (14, 74, 46))
    comp(img, copper, (30, 112, 70))
    edge = copper.filter(ImageFilter.FIND_EDGES).point(lambda v: min(255, v * 2))
    comp(img, edge, (60, 150, 98), 0.35)
    pads = ImageChops.multiply(mask_open, copper)
    comp(img, mask_open, (150, 120, 60))  # bare FR-4 in openings without copper is rare; tint anyway
    comp(img, pads, (214, 176, 98))
    comp(img, silk, (238, 240, 236))
    comp(img, drills, (18, 20, 22))

    # Parts.
    fp = json.load(open(os.path.join(tmp, 'FlyingProbeTesting.json')))['components']['rows']
    bom = {r['位号']: r for r in csv.DictReader(open(os.path.join(DESIGN, cfg['bom']), encoding='utf-8-sig'))}
    bodies = {}
    if cfg['bodies']:
        for b in json.load(open(os.path.join(DESIGN, cfg['bodies']), encoding='utf-8')):
            x0, y0, x1, y1 = b['body_bottom_bbox_mm']
            # Bottom view -> top view: x' = W - x, y' = H - y (image y down: y_img = y_bottom).
            bodies[b['ref']] = (w - x1, y0, w - x0, y1)
    dr = ImageDraw.Draw(img)
    parts = []
    for _, ref, layer, x, y, ang in fp:
        if layer != 'T' or ref.startswith('PAD') or ref not in bom:
            continue
        pkg, mpn = bom[ref]['封装或规格'], bom[ref]['型号或数值']
        if bom[ref].get('装配状态', '装配') not in ('', '装配'):
            continue
        cx, cy = mil(x) - org[0], h - (mil(y) - org[1])  # image y down
        img_ang = -ang  # gerber CCW -> image (y down)
        key = pkg.split('；')[0].split('(DO')[0]
        if mpn in CONN:
            hh, mat = CONN[mpn]
            if ref in bodies:
                x0, y0, x1, y1 = bodies[ref]
                parts.append(dict(ref=ref, x=round((x0 + x1) / 2, 2), y=round((y0 + y1) / 2, 2), l=round(x1 - x0, 2), w=round(y1 - y0, 2), h=hh, m=mat, s='box', a=0))
                continue
            dims = {'KH-BS1220-2-SMT': (15, 15), 'PZ254V-11-03P': (7.6, 2.5), 'PZ254V-11-04P': (10.1, 2.5), 'PM254-1-15-Z-8.5': (38.1, 2.5)}.get(mpn, (5, 5))
            parts.append(dict(ref=ref, x=round(cx, 2), y=round(cy, 2), l=dims[0], w=dims[1], h=hh, m=mat, s='box', a=img_ang))
        elif key in FLAT:
            paint_part(dr, key, ref, cx, cy, img_ang, px)
        elif key in TALL or pkg.startswith('SMD,P=1.25mm') or pkg.startswith('CONN-SMD') or pkg == 'TH':
            if ref in bodies:
                x0, y0, x1, y1 = bodies[ref]
                hh = 4.25 if 'GH' in mpn or 'GHS' in mpn else 6
                parts.append(dict(ref=ref, x=round((x0 + x1) / 2, 2), y=round((y0 + y1) / 2, 2), l=round(x1 - x0, 2), w=round(y1 - y0, 2), h=hh, m='housing', s='box', a=0))
                continue
            if pkg.startswith(('SMD,P=1.25mm', 'CONN-SMD')):
                n = int(''.join(c for c in pkg.split('P=1.25mm,')[-1] if c.isdigit()) or 8) if 'P=1.25mm,' in pkg else 2
                parts.append(dict(ref=ref, x=round(cx, 2), y=round(cy, 2), l=round(1.25 * (n - 1) + 3, 2), w=4.3, h=4.25, m='housing', s='box', a=img_ang))
                continue
            if pkg == 'TH':  # encoder
                parts.append(dict(ref=ref, x=round(cx, 2), y=round(cy, 2), l=12.4, w=13.4, h=6.5, m='metal', s='box', a=img_ang))
                continue
            l, ww, hh, mat, shape = TALL[key]
            parts.append(dict(ref=ref, x=round(cx, 2), y=round(cy, 2), l=l, w=ww, h=hh, m=mat, s=shape, a=img_ang))
        else:
            print('  unplaced', ref, pkg, file=sys.stderr)
    # Outside the board stays transparent (rounded corners).
    img = img.convert('RGBA')
    img.putalpha(inside)
    img.save(out_img, 'WEBP', quality=82, method=6)
    data = dict(w=w, h=h, tex='img/' + os.path.basename(out_img), parts=parts)
    with open(out_js, 'w', encoding='utf-8') as f:
        f.write('// Generated by tools/gen_pcb_twin.py from the board\'s Gerber, flying-probe\n'
                '// placement and BOM. Millimetres, x right, y down (top view).\n')
        f.write('window.PCB_BOARDS = window.PCB_BOARDS || {};\nwindow.PCB_BOARDS.' + name + ' = ' + json.dumps(data, ensure_ascii=False, separators=(',', ':')) + ';\n')
    print(name, f'{w}x{h}mm', size, len(parts), '3D parts', os.path.getsize(out_img) // 1024, 'KiB')


if __name__ == '__main__':
    for name, cfg in BOARDS.items():
        build(name, cfg, os.path.join(APP, 'web', 'img', f'pcb_{name}.webp'), os.path.join(APP, 'web', 'js', f'pcb_{name}.js'))
