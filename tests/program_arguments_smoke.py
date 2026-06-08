#!/usr/bin/env python3
import argparse
import subprocess
import tempfile
from pathlib import Path


PROGRAM_ARGS = ("doctor", "iki kelime", "--turkce-kati")
EXPECTED = "3\ndoctor\niki kelime\n--turkce-kati\n"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def assert_output(proc: subprocess.CompletedProcess[str], label: str) -> None:
    combined = (proc.stdout + proc.stderr).replace("\r\n", "\n")
    require(proc.returncode == 0, f"{label} failed: {combined}")
    require(
        combined == EXPECTED,
        f"{label} argument output mismatch: expected={EXPECTED!r} actual={combined!r}",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Orhun program argument smoke")
    parser.add_argument("binary", help="Orhun executable path")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    binary = Path(args.binary).resolve()
    require(binary.exists(), f"Binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="orhun_program_args_") as tmp:
        tmpdir = Path(tmp)
        source = tmpdir / "args.oh"
        source.write_text(
            "yazdır uzunluk(sistem.argumanlar)\n"
            "yazdır sistem.argumanlar[0]\n"
            "yazdır sistem.argumanlar[1]\n"
            "yazdır sistem.argumanlar[2]\n",
            encoding="utf-8",
            newline="\n",
        )

        modes = (
            ("default", [str(binary), str(source), *PROGRAM_ARGS]),
            ("vm", [str(binary), "vm", str(source), *PROGRAM_ARGS]),
            ("vm-kati", [str(binary), "vm-kati", str(source), *PROGRAM_ARGS]),
            ("yorumla", [str(binary), "yorumla", str(source), *PROGRAM_ARGS]),
            (
                "orhun-vm",
                [str(binary), "orhun-vm", str(source), "--source", "--", *PROGRAM_ARGS],
            ),
        )
        for label, command in modes:
            assert_output(run(command, repo), label)

        artifact = tmpdir / "args_artifact"
        compile_proc = run([str(binary), "derle", str(source), str(artifact)], repo)
        require(
            compile_proc.returncode == 0,
            "argument artifact compile failed: "
            + (compile_proc.stdout + compile_proc.stderr),
        )
        assert_output(
            run([str(binary), "obc", str(artifact.with_suffix(".obc")), *PROGRAM_ARGS], repo),
            "obc",
        )
        assert_output(run([str(artifact.with_suffix(".exe")), *PROGRAM_ARGS], repo), "package")

        toolchain = tmpdir / "toolchain"
        prepare_proc = run(
            [str(binary), "bootstrap-hazirla", str(toolchain)],
            repo,
        )
        require(
            prepare_proc.returncode == 0,
            "argument bootstrap prepare failed: "
            + (prepare_proc.stdout + prepare_proc.stderr),
        )
        assert_output(
            run(
                [
                    str(binary),
                    "bootstrap-calistir",
                    str(toolchain),
                    str(source),
                    *PROGRAM_ARGS,
                ],
                repo,
            ),
            "bootstrap-calistir",
        )

    print("Program argument smoke passed (8 execution paths).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
