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


def normalized(
    tokens: list[dict],
    include_position: bool,
    include_error_value: bool,
) -> list[dict]:
    keys = ("tur", "deger", "satir", "sutun") if include_position else ("tur", "deger")
    result = []
    for token in tokens:
        item = {key: token.get(key) for key in keys}
        if token.get("tur") == "HATA" and not include_error_value:
            item.pop("deger", None)
        result.append(item)
    return result


def fixture_include_position(path: Path) -> bool:
    try:
        first_line = path.read_text(encoding="utf-8").splitlines()[0]
    except IndexError:
        return True
    return "parity: tokens-only" not in first_line


def fixture_allow_errors(path: Path) -> bool:
    try:
        first_line = path.read_text(encoding="utf-8").splitlines()[0]
    except IndexError:
        return False
    return "parity: allow-errors" in first_line


def compare_file(
    binary: Path,
    repo: Path,
    source_file: Path,
    include_position: bool,
    allow_errors: bool,
) -> None:
    with tempfile.TemporaryDirectory(prefix="orhun_lexer_parity_") as tmp:
        root = Path(tmp)
        driver_file = root / "driver.oh"

        cxx_proc = run_cmd([str(binary), "lex", str(source_file), "--json"], repo)
        if allow_errors:
            require(
                cxx_proc.returncode != 0,
                f"C++ lexer should fail for error fixture: {source_file}",
            )
        else:
            require(
                cxx_proc.returncode == 0,
                f"C++ lexer command failed:\nSTDOUT:\n{cxx_proc.stdout}\nSTDERR:\n{cxx_proc.stderr}",
            )
        cxx_payload = parse_last_json(cxx_proc.stdout)
        if allow_errors:
            require(
                cxx_payload.get("hata_sayisi", 0) > 0,
                f"C++ lexer should report errors for {source_file}",
            )
        else:
            require(cxx_payload.get("hata_sayisi") == 0, "C++ lexer reported errors")
        cxx_tokens = cxx_payload.get("tokenlar")
        require(isinstance(cxx_tokens, list), "C++ lexer JSON missing tokenlar list")

        source_path = source_file.resolve().as_posix()
        driver_file.write_text(
            'lexer olsun dahil_et "orhun/lexer.oh"\n'
            f'kaynak olsun dosya.oku("{orhun_string(source_path)}")\n'
            "sonuc olsun lexer.ozetle(kaynak)\n"
            "yazdır json.yaz(sonuc)\n",
            encoding="utf-8",
            newline="\n",
        )

        orhun_proc = run_cmd([str(binary), str(driver_file)], repo)
        require(
            orhun_proc.returncode == 0,
            f"Orhun lexer driver failed:\nSTDOUT:\n{orhun_proc.stdout}\nSTDERR:\n{orhun_proc.stderr}",
        )
        orhun_payload = parse_last_json(orhun_proc.stdout)
        require(isinstance(orhun_payload, dict), "Orhun lexer output is not an object")
        require(
            orhun_payload.get("hata_sayisi") == cxx_payload.get("hata_sayisi"),
            f"Lexer error count mismatch: {source_file}\n"
            f"C++:   {cxx_payload.get('hata_sayisi')}\n"
            f"Orhun: {orhun_payload.get('hata_sayisi')}",
        )
        orhun_tokens = orhun_payload.get("tokenlar")
        require(isinstance(orhun_tokens, list), "Orhun lexer payload missing tokenlar list")

        left = normalized(cxx_tokens, include_position, include_error_value=not allow_errors)
        right = normalized(orhun_tokens, include_position, include_error_value=not allow_errors)
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
    allow_errors: bool = False,
    newline: str = "\n",
) -> None:
    with tempfile.TemporaryDirectory(prefix="orhun_lexer_parity_src_") as tmp:
        source_file = Path(tmp) / "case.oh"
        source_file.write_text(source, encoding="utf-8", newline=newline)
        compare_file(binary, repo, source_file, include_position, allow_errors)


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

    error_cases = 0
    tokens_only_cases = 0
    for case in cases:
        include_position = False if args.tokens_only else fixture_include_position(case)
        allow_errors = fixture_allow_errors(case)
        if allow_errors:
            error_cases += 1
        if not include_position:
            tokens_only_cases += 1
        compare_file(binary, repo, case, include_position, allow_errors)

    compare_source(
        binary,
        repo,
        "a olsun 1\n"
        "b olsun 2\n",
        include_position=False,
        newline="\r\n",
    )

    ok_cases = len(cases) - error_cases
    if args.tokens_only:
        print(
            f"Lexer parity smoke passed ({len(cases)} token-only fixture cases, "
            "1 CRLF inline case)."
        )
    else:
        print(
            "Lexer parity smoke passed "
            f"({ok_cases} ok fixture, {error_cases} error fixture, "
            f"{tokens_only_cases} tokens-only fixture, 1 CRLF inline case)."
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
