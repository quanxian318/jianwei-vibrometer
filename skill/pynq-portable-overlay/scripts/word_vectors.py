"""Generate and compare deterministic unsigned 32-bit stream vectors."""
import argparse
from itertools import zip_longest
from pathlib import Path
from random import Random


def words(path):
    for line_number, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        if len(line) != 8 or any(c not in "0123456789abcdefABCDEF" for c in line):
            raise ValueError(f"{path}:{line_number}: expected eight hex digits")
        yield int(line, 16)


def generate(args):
    args.directory.mkdir(parents=True, exist_ok=True)
    rng = Random(args.seed)
    boundary = [0, 1, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff]
    inputs = (boundary + [rng.getrandbits(32) for _ in range(max(0, args.count - len(boundary)))])[:args.count]
    delta = int(args.operation == "add-one")
    for name, values in (
        ("input.hex", inputs),
        ("golden.hex", ((value + delta) & 0xffffffff for value in inputs)),
    ):
        (args.directory / name).write_text("".join(f"{value:08x}\n" for value in values), encoding="ascii")


def compare(args):
    first = None
    mismatches = count = 0
    for index, (expected, actual) in enumerate(zip_longest(words(args.golden), words(args.actual))):
        count += 1
        if expected != actual:
            mismatches += 1
            if first is None:
                first = (index, expected, actual)
    if not count:
        raise ValueError("empty comparison")
    if mismatches:
        raise ValueError(f"{mismatches}/{count} mismatches; first (index, expected, actual) = {first}")
    print(f"PASS {count} words, bit exact")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    gen = sub.add_parser("generate")
    gen.add_argument("directory", type=Path)
    gen.add_argument("--count", type=int, default=64)
    gen.add_argument("--seed", type=int, default=42)
    gen.add_argument("--operation", choices=["identity", "add-one"], default="add-one")
    cmp = sub.add_parser("compare")
    cmp.add_argument("golden", type=Path)
    cmp.add_argument("actual", type=Path)
    args = parser.parse_args()
    try:
        if args.mode == "generate":
            if not 1 <= args.count <= 65536:
                raise ValueError("count must be in [1, 65536]")
            generate(args)
        else:
            compare(args)
    except (OSError, ValueError) as exc:
        parser.exit(2, f"{exc}\n")


if __name__ == "__main__":
    main()

