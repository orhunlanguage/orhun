#!/usr/bin/env python3
import re
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def read_module_version(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    match = re.search(r'surum\s+olsun\s+"([^"]+)"', text)
    require(match is not None, f"Module missing surum: {path}")
    return match.group(1)


def main() -> int:
    repo = Path.cwd()
    case_dir = repo / "tests" / "cases"
    module_dir = repo / "StdLib" / "orhun"

    checked = 0
    for fixture in sorted(case_dir.glob("stdlib_orhun_*.oh")):
        expected = fixture.with_name(f"{fixture.stem}.expected.txt")
        if not expected.exists():
            continue

        source = fixture.read_text(encoding="utf-8")
        include = re.search(
            r'([A-Za-z_][A-Za-z0-9_]*)\s+olsun\s+dahil_et\s+"orhun/([^"]+)\.oh"',
            source,
        )
        if include is None:
            continue

        variable, module = include.groups()
        if f"yazdır {variable}.surum" not in source:
            continue

        module_file = module_dir / f"{module}.oh"
        require(module_file.exists(), f"Stdlib module not found: {module_file}")
        module_version = read_module_version(module_file)

        expected_lines = expected.read_text(encoding="utf-8").splitlines()
        require(expected_lines, f"Expected output is empty: {expected}")
        expected_version = expected_lines[0].strip()
        require(
            expected_version == module_version,
            f"{expected.name} starts with {expected_version}, "
            f"but {module_file.name} declares {module_version}",
        )
        checked += 1

    require(checked > 0, "No stdlib version fixtures were checked")
    print(f"Stdlib version smoke passed ({checked} fixtures).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
