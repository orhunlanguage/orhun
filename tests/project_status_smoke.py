#!/usr/bin/env python3
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    repo = Path.cwd()
    version = (repo / "VERSION").read_text(encoding="utf-8").strip()
    readme = (repo / "README.md").read_text(encoding="utf-8")
    status = (repo / "docs" / "PROJECT_STATUS.md").read_text(encoding="utf-8")
    versioning = (repo / "docs" / "VERSIONING.md").read_text(encoding="utf-8")

    require(f"Current version: `{version}`" in readme, "README version is stale")
    require(f"Public version: `{version}`" in status, "project status version is stale")
    require("2.1.0" in status, "project status must mention the 2.1.0 product bar")
    require("2.1.0 Product Bar" in versioning, "versioning doc must define 2.1.0")
    require("docs/PROJECT_STATUS.md" in readme, "README should link project status")

    print(f"Project status smoke passed ({version}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
