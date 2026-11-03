#!/usr/bin/env python3
"""Convert a Vivado .bit into the raw, word-swapped bitstream that
/dev/xdevcfg (prepare_an5642_runtime.py) loads, as bootgen -process_bitstream bin."""
import argparse
import array
import struct
import sys


def raw_bitstream(data):
    pos = 2 + struct.unpack('>H', data[0:2])[0] + 2  # header field, then 0x0001
    while pos < len(data):
        key = data[pos:pos + 1]
        pos += 1
        if key == b'e':
            length = struct.unpack('>I', data[pos:pos + 4])[0]
            return data[pos + 4:pos + 4 + length]
        pos += 2 + struct.unpack('>H', data[pos:pos + 2])[0]  # a: design, b: part, c: date, d: time
    raise ValueError('no bitstream field in the .bit header')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('bit')
    ap.add_argument('bin')
    args = ap.parse_args()
    raw = raw_bitstream(open(args.bit, 'rb').read())
    if len(raw) % 4:
        raw += b'\xff' * (4 - len(raw) % 4)
    words = array.array('I')
    words.frombytes(raw)
    words.byteswap()
    out = words.tobytes()
    if b'\x66\x55\x99\xaa' not in out[:1024]:
        sys.exit('sync word not found: not a 7-series bitstream')
    open(args.bin, 'wb').write(out)
    print('%s: %d bytes' % (args.bin, len(out)))


if __name__ == '__main__':
    main()
