"""Validate a small PYNQ contract and generate a board smoke test."""
import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent


def load(path):
    c = json.loads(path.read_text(encoding="utf-8"))
    if c.get("schema") != 1 or c.get("dtype") != "uint32":
        raise ValueError("expected schema 1 and uint32")
    for key in ("dma", "ip"):
        if not re.fullmatch(r"[A-Za-z_]\w*(/[A-Za-z_]\w*)*", c[key], re.ASCII):
            raise ValueError(f"invalid {key} instance path")
    if not re.fullmatch(r"[a-zA-Z0-9_-]+\.bit", c["bit"]):
        raise ValueError("bit must be a filename")
    off = c["read_only_offset"]
    if type(off) is not int or off < 0 or off % 4:
        raise ValueError("read_only_offset must be an aligned byte offset")
    if type(c["samples"]) is not int or not 4 <= c["samples"] <= 1024:
        raise ValueError("samples must be in [4, 1024] for this smoke test")
    if c["reference"] not in ("identity", "add-one"):
        raise ValueError("unknown reference")
    return c


def preflight(c, root):
    bit = root / c["bit"]
    if not bit.is_file() or not bit.stat().st_size:
        raise ValueError(f"missing or empty {bit}")
    names = {
        v.strip("/") for m in ET.parse(bit.with_suffix(".hwh")).iter()
        for k in ("FULLNAME", "INSTANCE") if (v := m.get(k))
    }
    for key in ("dma", "ip"):
        if c[key] not in names:
            raise ValueError(f"HWH does not advertise {c[key]}")
    print("Overlay files and HWH instance names OK; board and provenance unverified")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("contract", type=Path)
    p.add_argument("--overlay-dir", type=Path)
    p.add_argument("--emit", type=Path)
    a = p.parse_args()
    try:
        c = load(a.contract)
        if a.overlay_dir:
            preflight(c, a.overlay_dir)
        if a.emit:
            template = (HERE / "board_smoke.py.in").read_text(encoding="utf-8")
            a.emit.write_text(template.replace("__CONTRACT__", repr(c)), encoding="utf-8")
        print("Contract OK")
    except (ValueError, KeyError, TypeError, OSError, ET.ParseError) as e:
        p.exit(2, f"contract error: {e}\n")


if __name__ == "__main__":
    main()

