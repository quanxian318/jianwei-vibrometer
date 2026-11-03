#!/usr/bin/env python3
"""Adds classes.dex to the aapt2 output and 4-byte aligns stored entries
(zipalign equivalent; resources.arsc must stay uncompressed and aligned).
  apk_pack.py base.apk classes.dex out.apk
"""
import sys
import zipfile

base, dex, out = sys.argv[1:4]
src = zipfile.ZipFile(base)
items = [(i, src.read(i.filename)) for i in src.infolist()]
items.append((zipfile.ZipInfo('classes.dex', date_time=(2026, 10, 25, 0, 0, 0)), open(dex, 'rb').read()))
STORE = ('resources.arsc', '.png', '.woff2', '.jpg')
with open(out, 'wb') as f:
    z = zipfile.ZipFile(f, 'w')
    for info, data in items:
        zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
        zi.compress_type = zipfile.ZIP_STORED if info.filename.endswith(STORE) else zipfile.ZIP_DEFLATED
        zi.external_attr = info.external_attr
        if zi.compress_type == zipfile.ZIP_STORED:
            # Local header: 30 bytes + name + extra; pad extra so the data starts on 4 bytes.
            offset = f.tell() + 30 + len(zi.filename.encode())
            zi.extra = b'\0' * ((4 - offset % 4) % 4)
        z.writestr(zi, data)
    z.close()
chk = zipfile.ZipFile(out)
for i in chk.infolist():
    if i.compress_type == zipfile.ZIP_STORED:
        start = i.header_offset + 30 + len(i.filename.encode()) + len(i.extra)
        assert start % 4 == 0, i.filename
print('packed', out, len(chk.infolist()), 'entries')
