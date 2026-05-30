#!/usr/bin/env python3
import argparse
import subprocess
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=cwd,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="package scaffold smoke test")
    parser.add_argument("binary", help="Orhun executable path")
    args = parser.parse_args()

    binary = Path(args.binary).resolve()
    require(binary.exists(), f"Binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="orhun_package_new_") as tmp:
        root = Path(tmp)
        proc = run([str(binary), "paket", "yeni", "kolay_paket"], root)
        require(
            proc.returncode == 0,
            f"paket yeni failed\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}",
        )

        project = root / "kolay_paket"
        main_file = project / "main.oh"
        config_file = project / "orhun.yap"
        require(main_file.exists(), "package scaffold missing main.oh")
        require(config_file.exists(), "package scaffold missing orhun.yap")

        source = main_file.read_text(encoding="utf-8")
        require(
            'yaz "Merhaba Orhun!"' in source,
            "package scaffold should use the beginner-friendly yaz command",
        )

        run_proc = run([str(binary), str(main_file)], root)
        require(
            run_proc.returncode == 0,
            f"generated package main failed\nSTDOUT:\n{run_proc.stdout}\nSTDERR:\n{run_proc.stderr}",
        )
        require(
            run_proc.stdout.strip() == "Merhaba Orhun!",
            f"generated package output mismatch: {run_proc.stdout!r}",
        )

    print("Package new smoke passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
