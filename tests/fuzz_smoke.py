#!/usr/bin/env python3
import argparse
import random
import subprocess
import tempfile
from pathlib import Path


IDS = [
    "a",
    "b",
    "x",
    "y",
    "sayac",
    "veri",
    "deger",
    "isim",
    "liste",
]
TEXTS = ['"orhun"', '"test"', '"abc"', '"42"']
OPS = ["+", "-", "*", "/"]


def rand_literal() -> str:
    r = random.random()
    if r < 0.35:
        return str(random.randint(-50, 200))
    if r < 0.65:
        return f"{random.randint(0, 20)}.{random.randint(0, 99)}"
    if r < 0.85:
        return random.choice(TEXTS)
    return random.choice(IDS)


def rand_expr(depth: int = 0) -> str:
    if depth > 2 or random.random() < 0.45:
        return rand_literal()
    left = rand_expr(depth + 1)
    right = rand_expr(depth + 1)
    op = random.choice(OPS)
    return f"{left} {op} {right}"


def rand_line() -> str:
    r = random.random()
    if r < 0.25:
        return f"{random.choice(IDS)} olsun {rand_expr()}"
    if r < 0.50:
        return f"{random.choice(IDS)} = {rand_expr()}"
    if r < 0.65:
        return f"yazdır {rand_expr()}"
    if r < 0.80:
        return f"eğer {rand_expr()} eşit {rand_expr()} ise: yazdır {rand_expr()}"
    if r < 0.90:
        return f"tekrarla {random.randint(0, 5)} kez yazdır {rand_expr()}"
    # Bilinçli bozuk satır: çökme olmamalı, hata mesajı üretmeli.
    return f"{random.choice(IDS)} olsun"


def rand_program(lines: int) -> str:
    out = []
    for _ in range(lines):
        out.append(rand_line())
    return "\n".join(out) + "\n"


def is_crash(returncode: int, stderr: str) -> bool:
    crash_codes = {134, 136, 139, -11, -6, 3221225477, 3221226505}
    if returncode in crash_codes:
        return True
    low = stderr.lower()
    return "segmentation fault" in low or "access violation" in low


def main() -> int:
    parser = argparse.ArgumentParser(description="Orhun lexer/parser/interpreter fuzz smoke")
    parser.add_argument("binary", nargs="?", default="orhun_test", help="Orhun executable path")
    parser.add_argument("--count", type=int, default=120, help="Fuzz iteration count")
    parser.add_argument("--seed", type=int, default=1337, help="Deterministic random seed")
    parser.add_argument("--timeout", type=float, default=2.0, help="Timeout per run (seconds)")
    args = parser.parse_args()

    random.seed(args.seed)
    binary = Path(args.binary)
    if not binary.exists():
        raise SystemExit(f"Binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="orhun_fuzz_") as td:
        base = Path(td)
        for i in range(args.count):
            src = base / f"fuzz_{i}.oh"
            src.write_text(rand_program(random.randint(1, 20)), encoding="utf-8")
            try:
                proc = subprocess.run(
                    [str(binary), str(src)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    timeout=args.timeout,
                    check=False,
                )
            except subprocess.TimeoutExpired:
                print(f"[FAIL] timeout at iteration {i}")
                print(src.read_text(encoding="utf-8"))
                return 1

            if is_crash(proc.returncode, proc.stderr):
                print(f"[FAIL] crash at iteration {i}, return={proc.returncode}")
                print(src.read_text(encoding="utf-8"))
                print(proc.stderr)
                return 1

            if (i + 1) % 20 == 0:
                print(f"[OK] {i + 1}/{args.count}")

    print("Fuzz smoke completed without crash.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
