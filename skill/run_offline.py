"""Run the bundled host checks and retain their evidence."""
import argparse
import json
from pathlib import Path
import py_compile
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="new evidence directory")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True)
    reports = "skill/tests/fixtures/"
    golden = "skill/golden-compare/"
    pynq = "skill/pynq-portable-overlay/"
    reference = golden + "examples/reference.csv"
    smoke = output / "board_smoke.py"
    steps = [
        ("unit", 0, ["skill/tests/test_skills.py"]),
        ("sampling", 0, ["skill/sampling_check/check_sampling.py", "--fs", "45",
                         "--target", "22", "--search-max", "30"]),
        ("sampling_rejection", 2, ["skill/sampling_check/check_sampling.py", "--fs", "45",
                                   "--target", "30", "--search-max", "30"]),
        ("vivado_report", 0, ["skill/implementation_audit/audit_vivado.py",
                              "--util", reports + "utilization.rpt",
                              "--timing", reports + "timing.rpt"]),
        ("golden_pass", 0, [golden + "compare.py", reference, golden + "examples/pass.csv"]),
        ("golden_difference", 1, [golden + "compare.py", reference, golden + "examples/fail.csv"]),
        ("pynq_contract", 0, [pynq + "scripts/contract.py",
                              pynq + "examples/contract.json", "--emit", str(smoke)]),
    ]
    checks = {}
    record_cases = {
        "interface-contract": ("bus", "base", "missing"),
        "optimization-baseline": ("scope", "unit", "missing"),
        "reproduction-binding": ("revision", "bit_sha256", "invalid"),
    }
    for mode, (field, extra, failure) in record_cases.items():
        folder = ROOT / "skill" / mode
        reference = folder / "reference.json"
        invalid = json.loads(reference.read_text(encoding="utf-8"))
        if failure == "missing":
            del invalid[extra]
        else:
            invalid[extra] = "invalid-hash"
        invalid_file = output / f"{mode}-invalid.json"
        invalid_file.write_text(json.dumps(invalid), encoding="utf-8")
        for label, observed, expected, key in [
            ("match", folder / "match.json", 0, ""),
            ("mismatch", folder / "mismatch.json", 1, field),
            ("invalid", invalid_file, 2, extra),
        ]:
            name = f"{mode}_{label}"
            steps.append((name, expected, [
                "skill/compare_records.py", mode, str(reference), str(observed),
            ]))
            checks[name] = (label, key)
    review = "report/review-cases/"
    name = "project_scope_rejection"
    steps.append((name, 1, ["skill/compare_records.py", "optimization-baseline",
                            review + "baseline.json", review + "candidate.json"]))
    checks[name] = ("mismatch", "scope")
    summary = []
    for name, expected, command in steps:
        result = subprocess.run([sys.executable, *command], cwd=ROOT, capture_output=True)
        (output / f"{name}.stdout").write_bytes(result.stdout)
        (output / f"{name}.stderr").write_bytes(result.stderr)
        passed = result.returncode == expected
        if passed and name == "vivado_report":
            report = json.loads(result.stdout)
            passed = (report["constrained_paths_pass"] is True
                      and report["listed_coverage_checks_clear"] is False)
        if passed and name == "sampling_rejection":
            passed = json.loads(result.stdout)["necessary_checks_passed"] is False
        if passed and name in checks:
            label, key = checks[name]
            report = json.loads(result.stdout)
            if label == "invalid":
                passed = key in report.get("error", "")
            else:
                passed = list(report.get("differences", {})) == ([key] if key else [])
        outcome = ("unexpected" if not passed else
                   "expected_rejection" if expected else "accepted")
        summary.append(dict(step=name, args=command, outcome=outcome, expected_exit=expected,
                            actual_exit=result.returncode, passed=passed))
    try:
        py_compile.compile(str(smoke), cfile=str(output / "board_smoke.pyc"), doraise=True)
        summary.append(dict(step="pynq_generated_syntax", passed=True))
    except (OSError, py_compile.PyCompileError) as error:
        summary.append(dict(step="pynq_generated_syntax", passed=False, error=str(error)))
    (output / "summary.json").write_text(json.dumps({
        "python": sys.version, "checks": summary,
    }, indent=2), encoding="utf-8")
    return 0 if all(step["passed"] for step in summary) else 1


if __name__ == "__main__":
    sys.exit(main())
