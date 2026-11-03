#!/usr/bin/env python3
"""Reserve the top 32 MiB of AX7010 DDR in the ALINX image.ub device tree.

The camera VDMA uses 0x1e000000..0x1f1fffff (six 3 MiB stores). The kernel
must not allocate these pages. Keep BOOT.BIN and the kernel unchanged; update
the embedded DT and its FIT hash. Keep the original image.ub for restoration.
"""
import argparse
import hashlib
from collections import OrderedDict
from pathlib import Path
import struct


def properties(blob):
    magic, size, pos, strings = struct.unpack_from('>4I', blob)
    if magic != 0xd00dfeed or size > len(blob):
        raise ValueError('not a complete FDT/FIT')
    stack, out = [], {}
    while True:
        token = struct.unpack_from('>I', blob, pos)[0]
        pos += 4
        if token == 1:
            end = blob.index(0, pos)
            stack.append(bytes(blob[pos:end]).decode())
            pos = (end + 4) & ~3
        elif token == 2:
            stack.pop()
        elif token == 3:
            length, name = struct.unpack_from('>II', blob, pos)
            pos += 8
            end = blob.index(0, strings + name)
            key = bytes(blob[strings + name:end]).decode()
            out[('/'.join(stack), key)] = (pos, length)
            pos = (pos + length + 3) & ~3
        elif token == 4:
            pass
        elif token == 9:
            return out
        else:
            raise ValueError('unknown FDT token')


def tree(blob):
    hdr = struct.unpack_from('>10I', blob)
    properties(blob)  # validate magic and token stream
    pos, strings = hdr[2], hdr[3]
    stack = []
    while True:
        token = struct.unpack_from('>I', blob, pos)[0]
        pos += 4
        if token == 1:
            end = blob.index(0, pos)
            n = {'name': blob[pos:end].decode(), 'props': OrderedDict(), 'children': []}
            if stack:
                stack[-1]['children'].append(n)
            else:
                root = n
            stack.append(n)
            pos = (end + 4) & ~3
        elif token == 2:
            stack.pop()
        elif token == 3:
            length, name = struct.unpack_from('>II', blob, pos)
            pos += 8
            end = blob.index(0, strings + name)
            stack[-1]['props'][blob[strings+name:end].decode()] = blob[pos:pos+length]
            pos = (pos + length + 3) & ~3
        elif token == 9:
            p = hdr[4]
            while blob[p:p+16] != b'\0' * 16:
                p += 16
            return root, blob[hdr[4]:p+16], hdr[7]


def encode(root, reservations, bootcpu):
    strings, tokens, offsets = bytearray(), bytearray(), {}

    def emit(n):
        tokens.extend(struct.pack('>I', 1) + n['name'].encode() + b'\0')
        tokens.extend(b'\0' * (-len(tokens) % 4))
        for key, value in n['props'].items():
            if key not in offsets:
                offsets[key] = len(strings)
                strings.extend(key.encode() + b'\0')
            tokens.extend(struct.pack('>III', 3, len(value), offsets[key]) + value)
            tokens.extend(b'\0' * (-len(tokens) % 4))
        for child in n['children']:
            emit(child)
        tokens.extend(struct.pack('>I', 2))

    emit(root)
    tokens.extend(struct.pack('>I', 9))
    off_struct = 40 + len(reservations)
    off_strings = off_struct + len(tokens)
    return (struct.pack('>10I', 0xd00dfeed, off_strings + len(strings), off_struct,
                        off_strings, 40, 17, 16, bootcpu, len(strings), len(tokens))
            + reservations + tokens + strings)


def child(n, name):
    return next(c for c in n['children'] if c['name'] == name)


def prepare(src):
    fit, reservations, bootcpu = tree(src)
    images = child(fit, 'images')
    for image in images['children']:
        for h in image['children']:
            if 'value' not in h['props']:
                continue
            algo = h['props']['algo'].rstrip(b'\0').decode()
            if hashlib.new(algo, image['props']['data']).digest() != h['props']['value']:
                raise ValueError('original FIT hash mismatch: ' + image['name'])
    fdt_image = child(images, 'fdt@0')
    dt, dt_reservations, dt_bootcpu = tree(fdt_image['props']['data'])
    memory = [n for n in dt['children'] if n['props'].get('device_type') == b'memory\0']
    if len(memory) != 1 or memory[0]['props']['reg'] != struct.pack('>II', 0, 0x20000000):
        raise ValueError('expected unmodified 512 MiB AX7010 DDR')
    # U-Boot overwrites /memory/reg with the detected 512 MiB. Reserve the
    # camera pages without no-map: this old kernel also reads the bootloader's
    # high-DDR FDT through its linear mapping during early memory setup.
    reserved = {'name': 'reserved-memory', 'props': OrderedDict([
        ('#address-cells', struct.pack('>I', 1)), ('#size-cells', struct.pack('>I', 1)),
        ('ranges', b'')]), 'children': [
            {'name': 'camera@1e000000', 'props': OrderedDict([
                ('reg', struct.pack('>II', 0x1e000000, 0x02000000))]), 'children': []}]}
    if any(n['name'] == 'reserved-memory' for n in dt['children']):
        raise ValueError('reserved-memory already exists; inspect it before changing the map')
    dt['children'].append(reserved)
    blob = encode(dt, dt_reservations, dt_bootcpu)
    fdt_image['props']['data'] = blob
    for h in fdt_image['children']:
        if 'value' in h['props']:
            h['props']['value'] = hashlib.new(h['props']['algo'].rstrip(b'\0').decode(), blob).digest()
    return encode(fit, reservations, bootcpu)


if __name__ == '__main__':
    a = argparse.ArgumentParser(description=__doc__)
    a.add_argument('original')
    a.add_argument('output')
    args = a.parse_args()
    result = prepare(Path(args.original).read_bytes())
    Path(args.output).write_bytes(result)
    print('Linux DDR: 480 MiB; camera DDR: 0x1E000000..0x1FFFFFFF')
    print('SHA256', hashlib.sha256(result).hexdigest())
