#!/usr/bin/env python3
import argparse
import json
import subprocess
import tempfile
from pathlib import Path


DEFAULT_FIXTURE_DIR = Path("tests/lexer_parity")


def run_cmd(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def parse_last_json(text: str):
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("json output is empty")
    return json.loads(lines[-1])


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def orhun_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def normalized(tokens: list[dict], include_position: bool) -> list[dict]:
    keys = ("tur", "deger", "satir", "sutun") if include_position else ("tur", "deger")
    return [{key: token.get(key) for key in keys} for token in tokens]


def fixture_include_position(path: Path) -> bool:
    try:
        first_line = path.read_text(encoding="utf-8").splitlines()[0]
    except IndexError:
        return True
    return "parity: tokens-only" not in first_line


def compare_file(binary: Path, repo: Path, source_file: Path, include_position: bool) -> None:
    with tempfile.TemporaryDirectory(prefix="orhun_lexer_parity_") as tmp:
        root = Path(tmp)
        driver_file = root / "driver.oh"

        cxx_proc = run_cmd([str(binary), "lex", str(source_file), "--json"], repo)
        require(
            cxx_proc.returncode == 0,
            f"C++ lexer command failed:\nSTDOUT:\n{cxx_proc.stdout}\nSTDERR:\n{cxx_proc.stderr}",
        )
        cxx_payload = parse_last_json(cxx_proc.stdout)
        require(cxx_payload.get("hata_sayisi") == 0, "C++ lexer reported errors")
        cxx_tokens = cxx_payload.get("tokenlar")
        require(isinstance(cxx_tokens, list), "C++ lexer JSON missing tokenlar list")

        source_path = source_file.resolve().as_posix()
        driver_file.write_text(
            'lexer olsun dahil_et "orhun/lexer.oh"\n'
            f'kaynak olsun dosya.oku("{orhun_string(source_path)}")\n'
            "tokenlar olsun lexer.tokenlestir(kaynak)\n"
            "yazdır json.yaz(tokenlar)\n",
            encoding="utf-8",
            newline="\n",
        )

        orhun_proc = run_cmd([str(binary), str(driver_file)], repo)
        require(
            orhun_proc.returncode == 0,
            f"Orhun lexer driver failed:\nSTDOUT:\n{orhun_proc.stdout}\nSTDERR:\n{orhun_proc.stderr}",
        )
        orhun_tokens = parse_last_json(orhun_proc.stdout)
        require(isinstance(orhun_tokens, list), "Orhun lexer output is not a list")

        left = normalized(cxx_tokens, include_position)
        right = normalized(orhun_tokens, include_position)
        require(
            left == right,
            f"Lexer parity mismatch: {source_file}\n"
            f"C++:   {json.dumps(left, ensure_ascii=False)}\n"
            f"Orhun: {json.dumps(right, ensure_ascii=False)}",
        )


def compare_source(
    binary: Path,
    repo: Path,
    source: str,
    include_position: bool,
    newline: str = "\n",
) -> None:
    with tempfile.TemporaryDirectory(prefix="orhun_lexer_parity_src_") as tmp:
        source_file = Path(tmp) / "case.oh"
        source_file.write_text(source, encoding="utf-8", newline=newline)
        compare_file(binary, repo, source_file, include_position)


def main() -> int:
    parser = argparse.ArgumentParser(description="C++ lexer / Orhun lexer parity smoke")
    parser.add_argument("binary", help="Orhun executable path")
    parser.add_argument(
        "--fixtures",
        default=str(DEFAULT_FIXTURE_DIR),
        help="Directory containing lexer parity .oh fixtures",
    )
    parser.add_argument(
        "--tokens-only",
        action="store_true",
        help="Compare only token types and values, ignoring line/column positions",
    )
    args = parser.parse_args()

    binary = Path(args.binary).resolve()
    if not binary.exists():
        raise SystemExit(f"Binary not found: {binary}")

    repo = Path.cwd()
    fixture_dir = (repo / args.fixtures).resolve()

    cases = sorted(fixture_dir.glob("*.oh")) if fixture_dir.exists() else []
    if not cases:
        compare_source(
            binary,
            repo,
            "blok:\n"
            "    x olsun 1\n"
            "    y olsun 10.5\n"
            "    yaz \"metin\"\n",
            include_position=True,
        )
        compare_source(
            binary,
            repo,
            "işlev ana():\n"
            "    yazdır \"Merhaba\"\n",
            include_position=False,
        )
        compare_source(
            binary,
            repo,
            "a olsun 1\n"
            "b olsun 2\n",
            include_position=False,
            newline="\r\n",
        )
        print("Lexer parity smoke passed (3 inline cases).")
        return 0

    for case in cases:
        include_position = False if args.tokens_only else fixture_include_position(case)
        compare_file(binary, repo, case, include_position)

    compare_source(
        binary,
        repo,
        "a olsun 1\n"
        "b olsun 2\n",
        include_position=False,
        newline="\r\n",
    )

    mode = " token-only" if args.tokens_only else ""
    print(f"Lexer parity smoke passed ({len(cases)}{mode} fixture cases, 1 CRLF inline case).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
