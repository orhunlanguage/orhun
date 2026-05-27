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


EXPECTED_CLOSURE_OUTPUT = """--- Test 1: Basit Closure ---
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


def normalize(text: str) -> str:
    return text.replace("\r\n", "\n").rstrip("\n")


def assert_closure_gap_resolved(
    proc: subprocess.CompletedProcess[str], label: str
) -> None:
    require(
        proc.returncode == 0,
        f"closure capture fixture failed in {label}: "
        f"{normalize(proc.stdout + proc.stderr)!r}",
    )
    require(
        normalize(proc.stdout + proc.stderr) == EXPECTED_CLOSURE_OUTPUT,
        f"closure capture output changed in {label}",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Known-gap regression guard")
    parser.add_argument("binary", help="Orhun executable")
    args = parser.parse_args()

    repo = Path.cwd()
    binary = Path(args.binary)
    require(binary.exists(), f"Binary not found: {binary}")

    closure_case = repo / "tests" / "cases" / "closure_missing_feature.oh"
    require(closure_case.exists(), f"Closure fixture not found: {closure_case}")

    assert_closure_gap_resolved(run(binary, closure_case), "default runner")
    assert_closure_gap_resolved(run(binary, closure_case, "vm-kati"), "vm-kati")

    print(
        "Known-gap smoke passed "
        "(closure capture promoted in default runner and vm-kati)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
