#!/usr/bin/env python3
from pathlib import Path


EXPECTEDLESS_CASES = {
    "closure_missing_feature",
    "math_features",
    "mod_in_func",
    "module_callable_lib",
    "primes_loop_caller",
    "primes_loop_once",
    "primes_mini",
    "primes_mini_loop",
    "primes_mini_loop_complex",
    "primes_mini_or",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    repo = Path.cwd()
    case_dir = repo / "tests" / "cases"
    ast_dir = repo / "tests" / "ast_json"

    cases = {path.stem for path in case_dir.glob("*.oh")}
    expected = {
        path.name.removesuffix(".expected.txt")
        for path in case_dir.glob("*.expected.txt")
    }
    ast_cases = {path.stem for path in ast_dir.glob("*.oh")}

    expected_without_case = sorted(expected - cases)
    require(
        not expected_without_case,
        "Expected files without matching .oh case: " + ", ".join(expected_without_case),
    )

    cases_without_expected = cases - expected
    unexpected_missing_expected = sorted(cases_without_expected - EXPECTEDLESS_CASES)
    require(
        not unexpected_missing_expected,
        "Cases without .expected.txt must be allowlisted: "
        + ", ".join(unexpected_missing_expected),
    )

    stale_allowlist = sorted(EXPECTEDLESS_CASES - cases_without_expected)
    require(
        not stale_allowlist,
        "EXPECTEDLESS_CASES contains stale entries: " + ", ".join(stale_allowlist),
    )

    missing_ast = sorted(cases - ast_cases)
    require(
        not missing_ast,
        "tests/cases entries missing tests/ast_json fixtures: " + ", ".join(missing_ast),
    )

    print(
        f"Case manifest smoke passed ({len(cases)} cases, "
        f"{len(expected)} expected files, {len(ast_cases)} AST fixtures)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
