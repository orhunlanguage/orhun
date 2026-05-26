#!/usr/bin/env python3
import argparse
from pathlib import Path

from capture_analysis_common import FunctionInfo, analyze_file, require


def assert_capture_expectations(
    results: dict[str, FunctionInfo],
    expected: dict[str, tuple[set[str], set[str], dict[str, int]]],
    label: str,
) -> int:
    for path, (captures, mutated_captures, capture_depths) in expected.items():
        require(
            path in results,
            f"{label}: closure analysis missing function path: {path}",
        )
        require(
            results[path].captures == captures,
            f"{label}: {path} captures changed: expected={sorted(captures)} "
            f"actual={sorted(results[path].captures)} refs={sorted(results[path].refs)} "
            f"locals={sorted(results[path].locals)}",
        )
        require(
            results[path].mutated_captures == mutated_captures,
            f"{label}: {path} mutated captures changed: "
            f"expected={sorted(mutated_captures)} "
            f"actual={sorted(results[path].mutated_captures)} "
            f"writes={sorted(results[path].writes)}",
        )
        require(
            results[path].capture_depths == capture_depths,
            f"{label}: {path} capture depths changed: expected={capture_depths} "
            f"actual={results[path].capture_depths}",
        )
    return len(expected)


def main() -> int:
    parser = argparse.ArgumentParser(description="Static closure capture analysis smoke")
    parser.add_argument("binary", help="Orhun executable")
    args = parser.parse_args()

    repo = Path.cwd()
    binary = Path(args.binary)
    require(binary.exists(), f"Binary not found: {binary}")

    known_gap_source = repo / "tests" / "cases" / "closure_missing_feature.oh"
    require(
        known_gap_source.exists(),
        f"Closure capture fixture not found: {known_gap_source}",
    )
    checked = assert_capture_expectations(
        analyze_file(binary, repo, known_gap_source),
        {
            "<program>.sayac_uret.artir": ({"adet"}, {"adet"}, {"adet": 1}),
            "<program>.dis_fonksiyon.ic_fonksiyon": (
                {"dis_x", "param"},
                set(),
                {"dis_x": 1, "param": 1},
            ),
            "<program>.banka_hesabi.para_yatir": (
                {"bakiye"},
                {"bakiye"},
                {"bakiye": 1},
            ),
            "<program>.banka_hesabi.para_cek": (
                {"bakiye"},
                {"bakiye"},
                {"bakiye": 1},
            ),
            "<program>.fonksiyon_listesi_uret.yazdir_func": (
                {"x"},
                set(),
                {"x": 1},
            ),
        },
        str(known_gap_source),
    )

    depth_source = repo / "tests" / "ast_json" / "closure_capture_depth.oh"
    require(
        depth_source.exists(),
        f"Closure capture depth fixture not found: {depth_source}",
    )
    checked += assert_capture_expectations(
        analyze_file(binary, repo, depth_source),
        {
            "<program>.dis.orta.ic": (
                {"a", "b"},
                set(),
                {"a": 2, "b": 1},
            ),
        },
        str(depth_source),
    )

    print(f"Closure capture analysis smoke passed ({checked} closures).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
