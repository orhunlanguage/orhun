#!/usr/bin/env python3
import argparse
import subprocess
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    parser = argparse.ArgumentParser(description="Interpreter CLI mode smoke")
    parser.add_argument("binary", help="Orhun executable")
    args = parser.parse_args()

    repo = Path.cwd()
    binary = Path(args.binary)
    require(binary.exists(), f"Binary not found: {binary}")

    source = repo / "tests" / "cases" / "assignment_equals_scope.oh"
    expected_path = repo / "tests" / "cases" / "assignment_equals_scope.expected.txt"
    require(source.exists(), f"Interpreter fixture not found: {source}")
    require(expected_path.exists(), f"Expected output not found: {expected_path}")

    proc = subprocess.run(
        [str(binary), "yorumla", str(source)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    combined = (proc.stdout + proc.stderr).replace("\r\n", "\n").rstrip("\n")
    expected = (
        expected_path.read_text(encoding="utf-8")
        .replace("\r\n", "\n")
        .rstrip("\n")
    )
    require(
        proc.returncode == 0,
        f"yorumla exited with {proc.returncode}: {combined}",
    )
    require(
        combined == expected,
        f"yorumla output changed: expected={expected!r} actual={combined!r}",
    )

    print("Interpreter mode smoke passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
