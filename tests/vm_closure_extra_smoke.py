#!/usr/bin/env python3
import argparse
import subprocess
import tempfile
from pathlib import Path


SOURCE = """işlev sonradan_guncelle():
    deger olsun 1
    işlev oku():
        döndür deger
    deger = 2
    döndür oku

okuyucu olsun sonradan_guncelle()
yazdır okuyucu()

global_x olsun 5
işlev golge():
    global_x olsun 10
    döndür işlev(): global_x

g olsun golge()
global_x = 20
yazdır g()

işlev gc_yakala():
    sakla olsun [1, 2, 3]
    işlev uzunluk_oku():
        döndür uzunluk(sakla)
    döndür uzunluk_oku

u olsun gc_yakala()
i olsun 0
sürece i küçük 3000:
    gecici olsun [i]
    i = i + 1

yazdır u()
"""

EXPECTED = """2
10
3"""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def normalize(text: str) -> str:
    return text.replace("\r\n", "\n").rstrip("\n")


def run(binary: Path, mode: str | None, source: Path) -> str:
    args = [str(binary)]
    if mode is not None:
        args.append(mode)
    args.append(str(source))
    proc = subprocess.run(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    combined = normalize(proc.stdout + proc.stderr)
    require(
        proc.returncode == 0,
        f"{mode or 'default'} exited with {proc.returncode}: {combined}",
    )
    return combined


def main() -> int:
    parser = argparse.ArgumentParser(description="Extra VM closure smoke")
    parser.add_argument("binary", help="Orhun executable")
    args = parser.parse_args()

    binary = Path(args.binary)
    require(binary.exists(), f"Binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="orhun-closure-") as tmp:
        source = Path(tmp) / "closure_extra.oh"
        source.write_text(SOURCE, encoding="utf-8")
        for mode in (None, "vm-kati", "yorumla"):
            actual = run(binary, mode, source)
            require(
                actual == EXPECTED,
                f"{mode or 'default'} closure output changed: "
                f"expected={EXPECTED!r} actual={actual!r}",
            )

    print("Extra VM closure smoke passed (default, vm-kati, yorumla).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
