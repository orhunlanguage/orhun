#!/usr/bin/env python3
import json
import re
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    syntax_path = repo / "tools" / "vscode-orhun" / "syntaxes" / "orhun.tmLanguage.json"
    grammar = json.loads(syntax_path.read_text(encoding="utf-8"))

    includes = [item.get("include") for item in grammar.get("patterns", [])]
    require("#builtins" in includes, "VS Code grammar should include #builtins")

    builtins = grammar.get("repository", {}).get("builtins", {}).get("patterns", [])
    require(builtins, "VS Code grammar missing builtins patterns")

    combined = "\n".join(str(pattern.get("match", "")) for pattern in builtins)
    for word in (
        "yaz",
        "oku",
        "aralik",
        "aralık",
        "ilk",
        "son",
        "bos_mu",
        "boş_mu",
        "dolu_mu",
        "json",
        "dosya",
    ):
        require(re.search(rf"(?<![A-Za-z_]){re.escape(word)}(?![A-Za-z_])", combined),
                f"VS Code builtins pattern missing {word}")

    print("VS Code syntax smoke passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
