"""PS/Nano protocol V1 byte reference. Python 3, standard library only."""
import argparse
import json
import struct

SOF = b'\xa5\x5a'
HEADER = struct.Struct('<BBBBHH')  # version, kind, opcode, flags, seq, payload_bytes
MAX_PAYLOAD = 1024
REQ, RESP, EVENT = 1, 2, 3


def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xffff if crc & 0x8000 else (crc << 1) & 0xffff
    return crc


def valid_header(v, kind, opcode, flags, seq, length):
    return (v == 1 and kind in (REQ, RESP, EVENT) and flags == 0
            and 0 <= opcode <= 255 and length <= MAX_PAYLOAD
            and ((kind == EVENT and seq == 0) or (kind != EVENT and 1 <= seq <= 65535)))


def encode(kind, opcode, seq, payload):
    if not isinstance(payload, dict):
        raise ValueError('payload must be an object')
    body = json.dumps(payload, ensure_ascii=False, separators=(',', ':'), allow_nan=False).encode('utf8')
    if not valid_header(1, kind, opcode, 0, seq, len(body)):
        raise ValueError('invalid header or payload length')
    data = HEADER.pack(1, kind, opcode, 0, seq, len(body)) + body
    return SOF + data + struct.pack('<H', crc16(data))


def _no_duplicates(pairs):
    result = {}
    for k, v in pairs:
        if k in result:
            raise ValueError('duplicate JSON key')
        result[k] = v
    return result


def _reject_constant(s):
    raise ValueError('nonfinite JSON number: ' + s)


def decode(frame):
    if len(frame) < 12 or frame[:2] != SOF:
        raise ValueError('not a frame')
    v, kind, opcode, flags, seq, length = HEADER.unpack(frame[2:10])
    if not valid_header(v, kind, opcode, flags, seq, length) or len(frame) != 12 + length:
        raise ValueError('invalid header or size')
    if crc16(frame[2:-2]) != struct.unpack('<H', frame[-2:])[0]:
        raise ValueError('bad CRC')
    obj = json.loads(frame[10:-2].decode('utf8'), object_pairs_hook=_no_duplicates,
                     parse_constant=_reject_constant)
    if not isinstance(obj, dict):
        raise ValueError('payload must be object')
    return {'version': v, 'kind': kind, 'opcode': opcode, 'seq': seq, 'payload': obj}


class StreamDecoder:
    """Call expire_partial() after the application's inter-byte deadline passes."""
    def __init__(self):
        self.buffer = bytearray()
        self.errors = 0

    def expire_partial(self):
        if self.buffer:
            self.errors += 1
            self.buffer.clear()

    def feed(self, data):
        frames = []
        # Consume bounded chunks so noise cannot grow the retained buffer without limit.
        for offset in range(0, len(data), 512):
            self.buffer.extend(data[offset:offset + 512])
            while True:
                start = self.buffer.find(SOF)
                if start < 0:
                    self.buffer[:] = b'\xa5' if self.buffer.endswith(b'\xa5') else b''
                    break
                if start:
                    del self.buffer[:start]
                if len(self.buffer) < 10:
                    break
                fields = HEADER.unpack(self.buffer[2:10])
                if not valid_header(*fields):
                    self.errors += 1
                    del self.buffer[0]
                    continue
                size = 12 + fields[-1]
                if len(self.buffer) < size:
                    break
                try:
                    frame = decode(bytes(self.buffer[:size]))
                except (ValueError, UnicodeError, RecursionError):
                    self.errors += 1
                    del self.buffer[0]
                    continue
                frames.append(frame)
                del self.buffer[:size]
        return frames


def vectors():
    cases = [
        ('hello_request', REQ, 1, 1, {'session_id': 'test-session', 'protocol': 1, 'ax_version': 'dev'}),
        ('hello_response', RESP, 1, 1, {'session_id': 'test-session', 'ok': True, 'code': 'OK', 'data': {'nano_boot_id': 'boot-test', 'protocol': 1}}),
        ('nfc_event', EVENT, 0x40, 0, {'nano_boot_id': 'boot-test', 'event_id': 1, 'event_type': 'NFC_TAG', 'uptime_ms': 2000, 'data': {'uid': '00112233', 'uid_len': 4}}),
        ('unicode_request', REQ, 0x20, 2, {'session_id': 'test-session', 'job_id': 'voice-1', 'asset_id': 'measure_done', 'volume': 20}),
    ]
    return [{'name': name, 'kind': kind, 'opcode': op, 'seq': seq, 'payload': p,
             'hex': encode(kind, op, seq, p).hex(' ')} for name, kind, op, seq, p in cases]


def self_test():
    import random
    assert crc16(b'123456789') == 0x29b1
    vv = vectors()
    wire = b''.join(bytes.fromhex(v['hex']) for v in vv)
    expected = [decode(bytes.fromhex(v['hex'])) for v in vv]
    decoder = StreamDecoder()
    assert decoder.feed(wire) == expected
    for seed in range(20):
        random.seed(seed)
        decoder = StreamDecoder(); got = []; offset = 0
        while offset < len(wire):
            n = random.randrange(1, 40)
            got += decoder.feed(wire[offset:offset+n]); offset += n
        assert got == expected
    first = bytes.fromhex(vv[0]['hex']); bad = bytearray(first); bad[10] ^= 1
    d = StreamDecoder()
    assert d.feed(b'noise\xa5' + bytes(bad) + wire) == expected and d.errors > 0
    oversize = SOF + HEADER.pack(1, REQ, 1, 0, 1, 1025)
    assert StreamDecoder().feed(oversize + first) == [expected[0]]
    d = StreamDecoder(); assert d.feed(first[:11]) == []; d.expire_partial()
    assert d.feed(first) == [expected[0]]
    edge = encode(REQ, 1, 9, {'x': 'a' * 1016})
    assert len(edge) == 1036 and decode(edge)['payload']['x'] == 'a' * 1016
    for p in [{'x': 'a' * 1017}, {'x': float('nan')}]:
        try: encode(REQ, 1, 1, p)
        except ValueError: pass
        else: raise AssertionError('invalid payload accepted')
    raw = b'{"x":1,"x":2}'
    h = HEADER.pack(1, REQ, 1, 0, 1, len(raw)) + raw
    duplicate = SOF + h + struct.pack('<H', crc16(h))
    assert StreamDecoder().feed(duplicate + first) == [expected[0]]
    assert StreamDecoder().feed(b'x' * 100000) == []
    return {'passed': True, 'checks': ['CRC known vector', 'roundtrip', '20 randomized fragmentation patterns', 'concatenation', 'noise and CRC recovery', 'oversize recovery', 'partial timeout', 'max payload', 'NaN rejection', 'duplicate-key rejection', 'bounded noise buffer']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--vectors', action='store_true')
    args = parser.parse_args()
    print(json.dumps(vectors() if args.vectors else self_test(), ensure_ascii=False, indent=2))
