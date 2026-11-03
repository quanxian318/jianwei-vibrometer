#!/usr/bin/env python3
"""Builds the app's bundled fonts (web/fonts) from the Fontsource npm packages.

  python3 tools/make_fonts.py <noto-sans-sc package dir> <inter-variable package dir>

Noto Sans SC is subset to the CJK characters that occur in web/ (device
messages and point names with other characters fall back to the system
font); Inter (variable, Latin) is copied. Both are SIL OFL 1.1.
Needs: pip install fonttools brotli
"""
import io
import pathlib
import re
import shutil
import sys

from fontTools import subset
from fontTools.merge import Merger
from fontTools.ttLib import TTFont

root = pathlib.Path(__file__).resolve().parents[1]
web = root / 'web'
sc_pkg, inter_pkg = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
out = web / 'fonts'
out.mkdir(exist_ok=True)

text = ''.join(p.read_text(encoding='utf-8') for p in list(web.rglob('*.js')) + list(web.rglob('*.html')))
# Common characters of device messages, levels and units besides the UI text.
text += '主频峰峰值正常预警报警未判级测点无明确质量检查失败相机未就绪协处理器离线采集分析中已完成双目深度声学温度湿度距离电压电流功率'
chars = sorted({c for c in text if ord(c) >= 0x2E80} | set('，。：；！？、（）《》“”‘’·…—～％'))
codes = {ord(c) for c in chars}
print(len(chars), 'CJK/punctuation characters')


def ranges(css):
    """subset file -> set of code points, from a Fontsource CSS file."""
    m = {}
    for block in re.findall(r'@font-face\s*{(.*?)}', css, re.S):
        f = re.search(r'files/([\w.-]+\.woff2)', block)
        r = re.search(r'unicode-range:\s*([^;]+);', block)
        if not f or not r:
            continue
        cps = set()
        for part in r.group(1).split(','):
            part = part.strip().replace('U+', '')
            if '-' in part:
                a, b = part.split('-')
                cps.update(range(int(a, 16), int(b, 16) + 1))
            elif part:
                cps.add(int(part, 16))
        m[f.group(1)] = cps
    return m


for weight in (400, 500, 700):
    files = ranges((sc_pkg / f'{weight}.css').read_text())
    parts = []
    for name, cps in files.items():
        need = codes & cps
        if not need:
            continue
        f = TTFont(sc_pkg / 'files' / name)
        opt = subset.Options()
        opt.layout_features = ['*']
        opt.name_IDs = ['*']
        opt.notdef_outline = True
        s = subset.Subsetter(opt)
        s.populate(unicodes=need)
        s.subset(f)
        f.flavor = None
        buf = io.BytesIO()
        f.save(buf)
        buf.seek(0)
        parts.append(buf)
    merged = Merger().merge(parts)
    merged.flavor = 'woff2'
    dst = out / f'NotoSansSC-{weight}.woff2'
    merged.save(dst)
    print(dst.name, len(parts), 'pieces', dst.stat().st_size // 1024, 'KiB')

shutil.copy(inter_pkg / 'files' / 'inter-latin-wght-normal.woff2', out / 'Inter-var.woff2')
print('Inter-var.woff2', (out / 'Inter-var.woff2').stat().st_size // 1024, 'KiB')
