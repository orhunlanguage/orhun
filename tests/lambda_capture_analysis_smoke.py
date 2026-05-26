#!/usr/bin/env python3
import argparse
from pathlib import Path

from capture_analysis_common import analyze_file, require


def main() -> int:
    parser = argparse.ArgumentParser(description="Static lambda capture analysis smoke")
    parser.add_argument("binary", help="Orhun executable")
    args = parser.parse_args()

    repo = Path.cwd()
    binary = Path(args.binary)
    require(binary.exists(), f"Binary not found: {binary}")

    cases = {
        repo / "tests" / "ast_json" / "lambda_capture_shadow.oh": {
            "<program>.<lambda@2>": (set(), set(), {}),
            "<program>.sar.<lambda@9>": ({"x"}, set(), {"x": 1}),
        },
        repo / "tests" / "ast_json" / "lambda_nested_return.oh": {
            "<program>.dis.<lambda@2>": ({"a"}, set(), {"a": 1}),
        },
        repo / "tests" / "ast_json" / "lambda_composed.oh": {
            "<program>.<lambda@2>": (set(), set(), {}),
            "<program>.<lambda@3>": (set(), set(), {}),
        },
    }

    checked = 0
    for source, expected in cases.items():
        require(source.exists(), f"Lambda capture fixture not found: {source}")
        results = analyze_file(binary, repo, source)
        for path, (captures, mutated_captures, capture_depths) in expected.items():
            require(path in results, f"{source}: missing lambda path {path}")
            require(
                results[path].captures == captures,
                f"{source}: {path} captures changed: expected={sorted(captures)} "
                f"actual={sorted(results[path].captures)} refs={sorted(results[path].refs)} "
                f"locals={sorted(results[path].locals)}",
            )
            require(
                results[path].mutated_captures == mutated_captures,
                f"{source}: {path} mutated captures changed: "
                f"expected={sorted(mutated_captures)} "
                f"actual={sorted(results[path].mutated_captures)} "
                f"writes={sorted(results[path].writes)}",
            )
            require(
                results[path].capture_depths == capture_depths,
                f"{source}: {path} capture depths changed: "
                f"expected={capture_depths} actual={results[path].capture_depths}",
            )
            checked += 1

    print(f"Lambda capture analysis smoke passed ({checked} lambdas).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
