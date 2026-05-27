#!/usr/bin/env python3
import argparse
import subprocess
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def normalize(text: str) -> str:
    return text.replace("\r\n", "\n").rstrip("\n")


def run_interpreter(binary: Path, source: Path) -> str:
    proc = subprocess.run(
        [str(binary), "yorumla", str(source)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    combined = normalize(proc.stdout + proc.stderr)
    require(
        proc.returncode == 0,
        f"yorumla exited with {proc.returncode} for {source.name}: {combined}",
    )
    return combined


def main() -> int:
    parser = argparse.ArgumentParser(description="Interpreter closure smoke")
    parser.add_argument("binary", help="Orhun executable")
    args = parser.parse_args()

    repo = Path.cwd()
    binary = Path(args.binary)
    require(binary.exists(), f"Binary not found: {binary}")

    closure_case = repo / "tests" / "cases" / "closure_missing_feature.oh"
    lambda_shadow_case = repo / "tests" / "cases" / "lambda_capture_shadow.oh"
    lambda_return_case = repo / "tests" / "cases" / "lambda_nested_return.oh"

    expected_closure = """--- Test 1: Basit Closure ---
1
2
1
3
--- Test 2: Ic Ice Closure ve Golgeleme ---
global
dis
ic
parametre
--- Test 3: Closure ile Degisken Degistirme ---
150
130
140
--- Test 4: Dongu Icinde Closure ---
0
1
2"""
    expected_lambda_shadow = """15
25
101"""
    expected_lambda_return = """8
6"""

    cases = [
        (closure_case, expected_closure),
        (lambda_shadow_case, expected_lambda_shadow),
        (lambda_return_case, expected_lambda_return),
    ]
    for source, expected in cases:
        require(source.exists(), f"Interpreter closure fixture not found: {source}")
        actual = run_interpreter(binary, source)
        require(
            actual == expected,
            f"yorumla closure output changed for {source.name}: "
            f"expected={expected!r} actual={actual!r}",
        )

    print(f"Interpreter closure smoke passed ({len(cases)} fixtures).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
