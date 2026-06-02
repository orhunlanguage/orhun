#!/usr/bin/env python3
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    guide_path = repo / "docs" / "BEGINNER_GUIDE.md"
    readme_path = repo / "README.md"

    require(guide_path.exists(), "Beginner guide is missing")
    guide = guide_path.read_text(encoding="utf-8")
    readme = readme_path.read_text(encoding="utf-8")

    for snippet in (
        'yaz "Merhaba, Orhun!"',
        'ad olsun oku("Adin? ")',
        "eğer puan büyük 50 ise:",
        "sayilar olsun aralik(1, 6)",
        "işlev selam",
        'dahil_et "orhun/koleksiyon.oh"',
        "koleksiyon.numaralandir",
        "koleksiyon.eslestir",
        "orhun vm-kati ilk.oh",
    ):
        require(snippet in guide, f"Beginner guide missing snippet: {snippet}")

    require(
        "docs/BEGINNER_GUIDE.md" in readme,
        "README should link to the beginner guide",
    )

    print("Beginner docs smoke passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
