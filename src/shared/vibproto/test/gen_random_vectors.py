"""Generate random frames with the reference codec for the C++ cross-check.

usage: python gen_random_vectors.py <dir containing protocol_reference.py> <out.json> [count]
"""
import json
import random
import sys

sys.path.insert(0, sys.argv[1])
import protocol_reference as ref  # noqa: E402

ALPHABET = 'abcXYZ019 _-:/"\\\n\t\x01\x1f测振仪数据é€😀'


def rand_str(r):
    return ''.join(r.choice(ALPHABET) for _ in range(r.randrange(0, 12)))


def rand_value(r, depth):
    choice = r.randrange(9 if depth < 3 else 6)
    if choice == 0:
        return None
    if choice == 1:
        return r.random() < 0.5
    if choice == 2:
        return r.randrange(-2**40, 2**40)
    if choice == 3:
        return round(r.uniform(-1e6, 1e6), r.randrange(0, 7))
    if choice == 4:
        return r.choice([0.5, -2.0, 1e-05, 1.5e-07, 3.25e+20, 0.1, 123456.789])
    if choice == 5:
        return rand_str(r)
    if choice in (6, 7):
        return {('k%d' % i) + rand_str(r): rand_value(r, depth + 1) for i in range(r.randrange(0, 5))}
    return [rand_value(r, depth + 1) for _ in range(r.randrange(0, 5))]


def main():
    out_path = sys.argv[2]
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    r = random.Random(20261020)
    cases = []
    while len(cases) < count:
        kind = r.choice([ref.REQ, ref.RESP, ref.EVENT])
        seq = 0 if kind == ref.EVENT else r.randrange(1, 65536)
        opcode = r.randrange(0, 256)
        payload = {('f%d' % i): rand_value(r, 1) for i in range(r.randrange(0, 6))}
        try:
            frame = ref.encode(kind, opcode, seq, payload)
        except ValueError:
            continue
        body = json.dumps(payload, ensure_ascii=False, separators=(',', ':'), allow_nan=False)
        cases.append({'kind': kind, 'opcode': opcode, 'seq': seq, 'payload_json': body, 'hex': frame.hex()})
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(cases, f, ensure_ascii=False)


if __name__ == '__main__':
    main()
