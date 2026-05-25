#!/usr/bin/env python3
import argparse
import re
import subprocess
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    parser = argparse.ArgumentParser(description="Orhun version consistency smoke")
    parser.add_argument("binary", help="Orhun executable")
    args = parser.parse_args()

    repo = Path.cwd()
    binary = Path(args.binary)
    require(binary.exists(), f"Binary not found: {binary}")

    version = (repo / "VERSION").read_text(encoding="utf-8").strip()
    require(version, "VERSION is empty")

    header = (repo / "Interpreter.h").read_text(encoding="utf-8")
    match = re.search(r'#define\s+ORHUN_SURUM\s+"([^"]+)"', header)
    require(match is not None, "Interpreter.h missing ORHUN_SURUM")
    require(
        match.group(1) == version,
        f"VERSION ({version}) != ORHUN_SURUM ({match.group(1)})",
    )

    readme = (repo / "README.md").read_text(encoding="utf-8")
    require(
        f"Current version: `{version}`" in readme,
        "README current version does not match VERSION",
    )

    proc = subprocess.run(
        [str(binary), "surum"],
        cwd=repo,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    require(proc.returncode == 0, f"surum command failed: {proc.stderr}")
    require(
        f"Orhun v{version}" in proc.stdout,
        f"surum output does not contain Orhun v{version}: {proc.stdout}",
    )

    print(f"Version consistency smoke passed ({version}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
