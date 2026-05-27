#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def resolve_binary(raw: str, repo: Path) -> Path:
    path = Path(raw)
    if path.is_absolute():
        return path

    cwd_candidate = (Path.cwd() / path).resolve()
    if cwd_candidate.exists():
        return cwd_candidate

    return (repo / path).resolve()


def last_non_empty_line(text: str) -> str:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return lines[-1] if lines else ""


def run_step(name: str, args: list[str], repo: Path) -> tuple[bool, str]:
    proc = subprocess.run(
        args,
        cwd=repo,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    command = " ".join(args)
    if proc.returncode == 0:
        detail = last_non_empty_line(proc.stdout) or "ok"
        return True, f"[OK] {name}: {detail}"

    output = "\n".join(
        part
        for part in (
            f"[FAIL] {name}",
            f"Command: {command}",
            "STDOUT:",
            proc.stdout.rstrip(),
            "STDERR:",
            proc.stderr.rstrip(),
        )
        if part
    )
    return False, output


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run Orhun roadmap smoke checks for local development"
    )
    parser.add_argument("binary", help="Orhun executable path")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    binary = resolve_binary(args.binary, repo)
    require(binary.exists(), f"Binary not found: {binary}")

    py = sys.executable
    steps = [
        ("case manifest", [py, "tests/case_manifest_smoke.py"]),
        ("version consistency", [py, "tests/version_consistency_smoke.py", str(binary)]),
        ("stdlib versions", [py, "tests/stdlib_version_smoke.py"]),
        ("known gaps", [py, "tests/known_gap_smoke.py", str(binary)]),
        ("closure capture analysis", [py, "tests/closure_capture_analysis_smoke.py", str(binary)]),
        ("lambda capture analysis", [py, "tests/lambda_capture_analysis_smoke.py", str(binary)]),
        ("interpreter mode", [py, "tests/interpreter_mode_smoke.py", str(binary)]),
        ("interpreter closures", [py, "tests/interpreter_closure_smoke.py", str(binary)]),
        ("lexer parity fixtures", [py, "tests/lexer_parity_smoke.py", str(binary)]),
        (
            "lexer token sweep",
            [
                py,
                "tests/lexer_parity_smoke.py",
                str(binary),
                "--fixtures",
                "tests/cases",
                "--tokens-only",
            ],
        ),
        ("AST JSON", [py, "tests/ast_json_smoke.py", str(binary)]),
        ("parser prototype", [py, "tests/parser_prototype_smoke.py", str(binary)]),
    ]

    failures = []
    for name, command in steps:
        ok, message = run_step(name, command, repo)
        print(message)
        if not ok:
            failures.append(name)

    require(
        not failures,
        "Roadmap smoke failed: " + ", ".join(failures),
    )
    print(f"Roadmap smoke passed ({len(steps)} checks).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
