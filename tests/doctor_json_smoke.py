#!/usr/bin/env python3
import argparse
import json
import subprocess
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    parser = argparse.ArgumentParser(description="Doctor JSON contract smoke")
    parser.add_argument("binary", help="Orhun executable")
    args = parser.parse_args()

    binary = Path(args.binary)
    if not binary.exists():
        raise SystemExit(f"Binary not found: {binary}")
    repo = Path(__file__).resolve().parents[1]

    proc = subprocess.run(
        [str(binary), "doctor", "--json"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    require(proc.returncode in (0, 2), f"doctor returned unexpected rc={proc.returncode}")

    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    require(lines, "doctor --json output is empty")
    try:
        payload = json.loads(lines[-1])
    except Exception as ex:
        raise RuntimeError(f"doctor --json is not valid JSON: {ex}") from ex

    for key in (
        "version",
        "build",
        "commit",
        "channel",
        "fallback_default",
        "fallback_source",
        "ci_profiles",
        "security_mode",
        "checks",
        "status",
    ):
        require(key in payload, f"doctor json missing key: {key}")

    expected_version = (repo / "VERSION").read_text(encoding="utf-8").strip()
    require(payload.get("version") == expected_version, "doctor version should match VERSION")
    require(payload.get("channel") == "stable", "doctor default channel should be stable")
    require(payload.get("fallback_default") is False, "stable fallback default should be false")
    require(payload.get("status") in ("ready", "warning"), "doctor status should be ready or warning")

    profiles = payload.get("ci_profiles")
    require(isinstance(profiles, list), "ci_profiles must be a list")
    for profile in ("ci", "nightly"):
        require(profile in profiles, f"ci_profiles missing {profile}")

    sec = payload.get("security_mode", {})
    require(isinstance(sec, dict), "security_mode must be an object")
    require(sec.get("ffi_policy_default") == "allowlist", "default FFI policy should be allowlist")
    require(
        sec.get("system_command_restricted_default") is True,
        "system commands should be restricted by default",
    )
    require(
        sec.get("package_source_allowlist") is True,
        "package source allowlist should be enabled by default",
    )

    checks = payload.get("checks", {})
    require(isinstance(checks, dict), "checks must be an object")
    for key in (
        "compiler_files",
        "stdlib_core",
        "test_infra",
        "lsp_tools",
        "benchmark_gate_scripts",
        "git_access",
    ):
        require(isinstance(checks.get(key), bool), f"checks.{key} should be bool")

    print("Doctor JSON smoke passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
