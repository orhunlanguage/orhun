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

    fixtures = (
        "assignment_equals_scope",
        "listeye_ekle_parity",
        "modulo_parity",
        "short_circuit_parity",
        "utf8_check",
    )
    for fixture in fixtures:
        source = repo / "tests" / "cases" / f"{fixture}.oh"
        expected_path = repo / "tests" / "cases" / f"{fixture}.expected.txt"
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
            f"yorumla exited with {proc.returncode} for {source}: {combined}",
        )
        require(
            combined == expected,
            f"yorumla output changed for {source}: "
            f"expected={expected!r} actual={combined!r}",
        )

    print(f"Interpreter mode smoke passed ({len(fixtures)} fixtures).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
