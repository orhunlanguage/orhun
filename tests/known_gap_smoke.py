#!/usr/bin/env python3
import argparse
import subprocess
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run(
    binary: Path, source: Path, mode: str | None = None
) -> subprocess.CompletedProcess[str]:
    args = [str(binary)]
    if mode is not None:
        args.append(mode)
    args.append(str(source))
    return subprocess.run(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def assert_closure_known_gap(
    proc: subprocess.CompletedProcess[str], label: str
) -> None:
    combined = proc.stdout + proc.stderr
    require(
        proc.returncode != 0,
        f"closure known-gap case now passes in {label}; promote it to a normal "
        "fixture and update docs/CLOSURE_CAPTURE_PLAN.md",
    )
    expected_signatures = (
        "Tanimsiz degisken: 'adet'",
        "Tanımsız değişken: 'adet'",
        "'adet' değişkeni bulunamadı",
        "'adet' degiskeni bulunamadi",
    )
    require(
        any(signature in combined for signature in expected_signatures),
        f"closure known-gap failure changed in {label}; update the guard and plan",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Known-gap guard for unfinished semantics")
    parser.add_argument("binary", help="Orhun executable")
    args = parser.parse_args()

    repo = Path.cwd()
    binary = Path(args.binary)
    require(binary.exists(), f"Binary not found: {binary}")

    closure_case = repo / "tests" / "cases" / "closure_missing_feature.oh"
    require(closure_case.exists(), f"Known-gap case not found: {closure_case}")

    assert_closure_known_gap(run(binary, closure_case), "default runner")
    assert_closure_known_gap(run(binary, closure_case, "vm-kati"), "vm-kati")

    print(
        "Known-gap smoke passed "
        "(closure capture still tracked in default runner and vm-kati)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
