#!/usr/bin/env python3
import argparse
import os
import subprocess
from pathlib import Path


def run(binary: Path, args: list[str], env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(binary)] + args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
    )


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def has_orh_comp_marker(proc: subprocess.CompletedProcess[str]) -> bool:
    return "orh-comp-" in (proc.stdout + proc.stderr).lower()


def main() -> int:
    parser = argparse.ArgumentParser(description="Fallback smoke for the ORH-COMP gate")
    parser.add_argument("binary", help="Orhun executable")
    parser.add_argument(
        "--case",
        default="tests/cases/closure_missing_feature.oh",
        help="Case used to exercise ORH-COMP fallback or the closed fallback gate",
    )
    args = parser.parse_args()

    binary = Path(args.binary)
    if not binary.exists():
        raise SystemExit(f"Binary not found: {binary}")

    case = Path(args.case)
    if not case.exists():
        raise SystemExit(f"Case not found: {case}")

    env_base = dict(os.environ)
    env_base["ORHUN_CHANNEL"] = "stable"

    env_off = dict(env_base)
    env_off["ORHUN_VM_FALLBACK"] = "0"
    p_off = run(binary, [str(case)], env_off)

    env_on = dict(env_base)
    env_on["ORHUN_VM_FALLBACK"] = "1"
    p_on = run(binary, [str(case)], env_on)

    p_strict = run(binary, ["vm-kati", str(case)], env_on)
    if has_orh_comp_marker(p_off):
        require(p_off.returncode != 0, "fallback=0 should fail on ORH-COMP input")
        require(p_on.returncode == 0, "fallback=1 should recover via interpreter")
        require(
            not has_orh_comp_marker(p_on),
            "fallback=1 output should not surface ORH-COMP error",
        )
        require(
            p_strict.returncode != 0,
            "vm-kati should fail for unsupported VM construct",
        )
        require(
            has_orh_comp_marker(p_strict),
            "vm-kati failure should expose ORH-COMP marker",
        )
        print("Fallback ORH-COMP smoke passed.")
        return 0

    require(
        not has_orh_comp_marker(p_on),
        "fallback=1 should not introduce ORH-COMP marker for non-ORH-COMP input",
    )
    require(
        not has_orh_comp_marker(p_strict),
        "vm-kati should not introduce ORH-COMP marker for non-ORH-COMP input",
    )
    require(
        p_on.returncode == p_off.returncode,
        "fallback=1 should not change non-ORH-COMP runtime outcome",
    )
    require(
        p_strict.returncode == p_off.returncode,
        "vm-kati should match default outcome when no ORH-COMP marker is present",
    )

    print("Fallback ORH-COMP smoke passed (no active ORH-COMP fixture).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
