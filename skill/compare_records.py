"""Compare required evidence fields in two independent records."""
import argparse
import json
from pathlib import Path
import re
import sys

SCHEMAS = {'interface-contract': {'build': 'str', 'bus': 'str', 'base': 'str', 'width': 'int', 'irq': 'str'}, 'optimization-baseline': {'device': 'str', 'input': 'str', 'scope': 'str', 'clock_mhz': 'int', 'tool': 'str', 'unit': 'str'}, 'reproduction-binding': {'revision': 'str', 'bit_sha256': 'str', 'config_sha256': 'str', 'cases': 'str', 'command': 'str'}}


def load(path, schema):
    record = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(record, dict):
        raise ValueError(f"{path}: expected JSON object")
    for field, kind in schema.items():
        value = record.get(field)
        if type(value).__name__ != kind or (kind == "str" and not value.strip()):
            raise ValueError(f"{path}: missing or invalid {field}")
        if field.endswith("_sha256") and not re.fullmatch(r"[0-9a-f]{64}", value):
            raise ValueError(f"{path}: invalid SHA256 in {field}")
        if kind == "int" and value <= 0:
            raise ValueError(f"{path}: {field} must be positive")
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=SCHEMAS)
    parser.add_argument("reference", type=Path)
    parser.add_argument("observed", type=Path)
    args = parser.parse_args()
    schema = SCHEMAS[args.mode]
    try:
        reference = load(args.reference, schema)
        observed = load(args.observed, schema)
    except (OSError, ValueError) as error:
        print(json.dumps({"error": str(error)}))
        return 2
    differences = {field: {"reference": reference[field], "observed": observed[field]}
                   for field in schema if reference[field] != observed[field]}
    print(json.dumps({"mode": args.mode, "differences": differences}))
    return int(bool(differences))


if __name__ == "__main__":
    sys.exit(main())
