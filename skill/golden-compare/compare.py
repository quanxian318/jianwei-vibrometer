import argparse
import csv
import json
import re
import sys


def read_values(path):
    values = {}
    with open(path, encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream, strict=True)
        if reader.fieldnames != ["case_id", "index", "value"]:
            raise ValueError(f"{path}: expected case_id,index,value")
        for row in reader:
            case = row["case_id"]
            index = row["index"]
            value = row["value"]
            if (None in row or not case or case.strip() != case
                    or not re.fullmatch(r"[0-9]+", index or "")
                    or not re.fullmatch(r"[+-]?[0-9]+", value or "")):
                raise ValueError(f"{path}:{reader.line_num}: invalid row")
            key = (case, int(index))
            if key in values:
                raise ValueError(f"{path}:{reader.line_num}: duplicate key {key}")
            values[key] = int(value)
    if not values:
        raise ValueError(f"{path}: no data rows")
    return values


def main():
    parser = argparse.ArgumentParser(description="Compare keyed integer CSV outputs")
    parser.add_argument("reference")
    parser.add_argument("actual")
    args = parser.parse_args()
    try:
        reference = read_values(args.reference)
        actual = read_values(args.actual)
    except (OSError, ValueError, csv.Error) as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=True))
        return 2
    differences = []
    for case, index in sorted(reference.keys() | actual.keys()):
        key = (case, index)
        expected, observed = reference.get(key), actual.get(key)
        if expected != observed:
            differences.append({
                "case_id": case, "index": index,
                "expected": expected, "actual": observed,
            })
    print(json.dumps({
        "reference_count": len(reference), "actual_count": len(actual),
        "difference_count": len(differences), "differences": differences,
    }, ensure_ascii=True))
    return int(bool(differences))


if __name__ == "__main__":
    sys.exit(main())
