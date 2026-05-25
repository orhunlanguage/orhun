#!/usr/bin/env python3
import argparse
import subprocess
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run(binary: Path, source: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(binary), str(source)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
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

    proc = run(binary, closure_case)
    combined = proc.stdout + proc.stderr
    require(
        proc.returncode != 0,
        "closure known-gap case now passes; promote it to a normal fixture and "
        "update docs/CLOSURE_CAPTURE_PLAN.md",
    )
    require(
        "Tanimsiz degisken: 'adet'" in combined or "Tanımsız değişken: 'adet'" in combined,
        "closure known-gap failure changed; update the known-gap guard and plan",
    )

    print("Known-gap smoke passed (closure capture still tracked).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
