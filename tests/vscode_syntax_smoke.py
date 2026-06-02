#!/usr/bin/env python3
import json
import re
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    package_path = repo / "tools" / "vscode-orhun" / "package.json"
    syntax_path = repo / "tools" / "vscode-orhun" / "syntaxes" / "orhun.tmLanguage.json"
    snippets_path = repo / "tools" / "vscode-orhun" / "snippets" / "orhun.code-snippets"

    package = json.loads(package_path.read_text(encoding="utf-8"))
    snippets = json.loads(snippets_path.read_text(encoding="utf-8"))
    grammar = json.loads(syntax_path.read_text(encoding="utf-8"))

    snippet_contribs = package.get("contributes", {}).get("snippets", [])
    require(snippet_contribs, "VS Code package should contribute snippets")
    require(
        any(
            item.get("language") == "orhun"
            and item.get("path") == "./snippets/orhun.code-snippets"
            for item in snippet_contribs
        ),
        "VS Code package snippet contribution is missing or stale",
    )

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

    required_snippets = {
        "Yaz": "yaz",
        "Oku": "oku",
        "Eğer": "eger",
        "İşlev": "islev",
        "Aralık Döngüsü": "aralik",
        "Numaralandır": "numaralandir",
        "Eşleştir": "eslestir",
    }
    for name, prefix in required_snippets.items():
        require(name in snippets, f"VS Code snippet missing {name}")
        snippet = snippets[name]
        require(snippet.get("prefix") == prefix, f"VS Code snippet prefix stale: {name}")
        body = "\n".join(snippet.get("body", []))
        require(body.strip(), f"VS Code snippet body empty: {name}")

    require("oku(" in "\n".join(snippets["Oku"]["body"]), "Oku snippet should call oku")
    require("aralik(" in "\n".join(snippets["Aralık Döngüsü"]["body"]),
            "Aralik snippet should call aralik")
    require("koleksiyon.numaralandir(" in "\n".join(snippets["Numaralandır"]["body"]),
            "Numaralandir snippet should call koleksiyon.numaralandir")
    require("koleksiyon.eslestir(" in "\n".join(snippets["Eşleştir"]["body"]),
            "Eslestir snippet should call koleksiyon.eslestir")

    print("VS Code tooling smoke passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
