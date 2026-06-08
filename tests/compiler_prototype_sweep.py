#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from compiler_prototype_smoke import parse_last_json, prototype_payload, require, run_cmd


EXPECTED_CXX_FAILURES = {
    "default_args_non_trailing_error.oh",
    "error_parser_typo_keyword.oh",
    "lambda_in_dict.oh",
}


def cxx_bytecode_or_none(binary: Path, repo: Path, source: Path) -> dict | None:
    proc = run_cmd([str(binary), "baytkod", str(source), "--json"], repo)
    if proc.returncode != 0:
        return None

    payload = parse_last_json(proc.stdout)
    require(payload.get("durum") == "ok", f"C++ payload failed for {source.name}")
    bytecode = payload.get("bytecode")
    require(isinstance(bytecode, dict), f"C++ bytecode missing for {source.name}")
    return bytecode


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Sweep all runtime cases for Orhun/C++ compiler bytecode parity"
    )
    parser.add_argument("binary", help="Orhun executable path")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    binary = Path(args.binary).resolve()
    require(binary.exists(), f"Binary not found: {binary}")

    exact = 0
    cxx_failures = set()
    with tempfile.TemporaryDirectory(prefix="orhun_compiler_sweep_") as tmp:
        tmpdir = Path(tmp)
        for source in sorted((repo / "tests" / "cases").glob("*.oh")):
            expected = cxx_bytecode_or_none(binary, repo, source)
            if expected is None:
                cxx_failures.add(source.name)
                continue

            payload = prototype_payload(binary, repo, source, tmpdir)
            require(
                payload.get("durum") == "ok" and payload.get("hata_sayisi") == 0,
                f"prototype returned error for {source.name}: {payload}",
            )
            require(
                payload.get("bytecode") == expected,
                f"Compiler prototype bytecode mismatch for {source.name}\n"
                f"C++: {expected}\nOrhun: {payload.get('bytecode')}",
            )
            exact += 1

    unexpected = sorted(cxx_failures - EXPECTED_CXX_FAILURES)
    require(
        not unexpected,
        "Unexpected C++ compiler failures: " + ", ".join(unexpected),
    )
    stale = sorted(EXPECTED_CXX_FAILURES - cxx_failures)
    require(
        not stale,
        "EXPECTED_CXX_FAILURES contains stale entries: " + ", ".join(stale),
    )

    print(
        f"Compiler prototype sweep passed ({exact} exact parity, "
        f"{len(cxx_failures)} expected C++ failures)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
