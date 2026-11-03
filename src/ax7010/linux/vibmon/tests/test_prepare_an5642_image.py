import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_an5642_image import child, encode, prepare, tree


def node(name, props=(), children=()):
    return {'name': name, 'props': dict(props), 'children': list(children)}


def fixture():
    dt = node('', [('#address-cells', struct.pack('>I', 1))], [
        node('memory@0', [('device_type', b'memory\0'), ('reg', struct.pack('>II', 0, 0x20000000))]),
        node('chosen', [('bootargs', b'root=/dev/mmcblk0p2 rw rootwait\0')])])
    dt = encode(dt, b'\0'*16, 0)
    def image(name, data):
        return node(name, [('data', data)], [node('hash@1', [
            ('algo', b'sha1\0'), ('value', hashlib.sha1(data).digest())])])
    fit = node('', children=[node('images', children=[image('kernel@0', b'unchanged kernel'), image('fdt@0', dt)]),
                             node('configurations', [('default', b'conf@2\0')])])
    return encode(fit, b'\0'*16, 0)


class PrepareImageTest(unittest.TestCase):
    def test_preserves_kernel_and_boot_config_and_reserves_dma(self):
        src = fixture()
        out, _, _ = tree(prepare(src))
        old, _, _ = tree(src)
        self.assertEqual(child(out, 'configurations'), child(old, 'configurations'))
        self.assertEqual(child(child(out, 'images'), 'kernel@0'), child(child(old, 'images'), 'kernel@0'))
        fdt = child(child(out, 'images'), 'fdt@0')
        dt, _, _ = tree(fdt['props']['data'])
        self.assertEqual(child(dt, 'memory@0')['props']['reg'], struct.pack('>II', 0, 0x20000000))
        region = child(child(dt, 'reserved-memory'), 'camera@1e000000')['props']
        self.assertEqual(region['reg'], struct.pack('>II', 0x1e000000, 0x2000000))
        self.assertNotIn('no-map', region)
        self.assertEqual(child(fdt, 'hash@1')['props']['value'], hashlib.sha1(fdt['props']['data']).digest())

    def test_rejects_bad_original_hash(self):
        src = fixture().replace(b'unchanged kernel', b'corrupted kernel')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            prepare(src)

    def test_does_not_duplicate_memory_reservations(self):
        with self.assertRaisesRegex(ValueError, 'already exists'):
            prepare(prepare(fixture()))


if __name__ == '__main__':
    unittest.main()
