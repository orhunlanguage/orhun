#!/usr/bin/env python3
import argparse
import json
import subprocess
import tempfile
from pathlib import Path


DEFAULT_FIXTURE_DIR = Path("tests/ast_json")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run_cmd(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=cwd,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def parse_last_json(text: str):
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    require(lines, "JSON output is empty")
    return json.loads(lines[-1])


def orhun_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def is_error_fixture(path: Path) -> bool:
    try:
        return path.read_text(encoding="utf-8").splitlines()[0].strip() == "# ast-error"
    except IndexError:
        return False


def cxx_parse_payload(binary: Path, repo: Path, source_file: Path, should_fail: bool) -> dict:
    proc = run_cmd([str(binary), "parse", str(source_file), "--json"], repo)
    if should_fail:
        require(
            proc.returncode == 1,
            f"C++ parse should fail for {source_file}:\n"
            f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}",
        )
        return parse_last_json(proc.stdout)

    require(
        proc.returncode == 0,
        f"C++ parse failed for {source_file}:\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}",
    )
    return parse_last_json(proc.stdout)


def cxx_top_level_nodes(payload: dict, source_file: Path) -> list[dict]:
    ast = payload.get("ast")
    require(isinstance(ast, dict), f"C++ parse payload missing ast for {source_file}")
    commands = ast.get("komutlar")
    require(isinstance(commands, list), f"C++ AST missing komutlar for {source_file}")
    return [{"tur": command.get("tur"), "satir": command.get("satir")} for command in commands]


def orhun_parser_payload(binary: Path, repo: Path, source_file: Path) -> dict:
    with tempfile.TemporaryDirectory(prefix="orhun_parser_proto_") as tmp:
        driver = Path(tmp) / "driver.oh"
        source_path = source_file.resolve().as_posix()
        driver.write_text(
            'parser olsun dahil_et "orhun/parser.oh"\n'
            f'kaynak olsun dosya.oku("{orhun_string(source_path)}")\n'
            "sonuc olsun parser.ozetle(kaynak)\n"
            "yazdır json.yaz(sonuc)\n",
            encoding="utf-8",
            newline="\n",
        )
        proc = run_cmd([str(binary), str(driver)], repo)
        require(
            proc.returncode == 0,
            f"Orhun parser prototype failed for {source_file}:\n"
            f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}",
        )
        payload = parse_last_json(proc.stdout)
        return payload


def orhun_parser_nodes(payload: dict, source_file: Path) -> list[dict]:
    require(payload.get("ok") is True, f"prototype returned error for {source_file}: {payload}")
    commands = payload.get("komutlar")
    require(isinstance(commands, list), f"prototype payload missing komutlar for {source_file}")
    return [{"tur": command.get("tur"), "satir": command.get("satir")} for command in commands]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare Orhun parser prototype against C++ AST top-level nodes"
    )
    parser.add_argument("binary", help="Orhun executable path")
    parser.add_argument(
        "--fixtures",
        default=str(DEFAULT_FIXTURE_DIR),
        help="Directory containing AST JSON .oh fixtures",
    )
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    binary = Path(args.binary).resolve()
    if not binary.exists():
        raise SystemExit(f"Binary not found: {binary}")

    fixture_dir = (repo / args.fixtures).resolve()
    cases = sorted(fixture_dir.glob("*.oh"))
    require(cases, f"No parser prototype fixtures found under {fixture_dir}")

    ok_count = 0
    error_count = 0
    for case in cases:
        should_fail = is_error_fixture(case)
        cxx_payload = cxx_parse_payload(binary, repo, case, should_fail)
        proto_payload = orhun_parser_payload(binary, repo, case)

        if should_fail:
            require(cxx_payload.get("durum") == "fail", f"C++ payload should fail for {case}")
            require(proto_payload.get("ok") is False, f"prototype should fail for {case}")
            error_count += 1
            continue

        cxx = cxx_top_level_nodes(cxx_payload, case)
        proto = orhun_parser_nodes(proto_payload, case)
        require(
            cxx == proto,
            f"Parser prototype node mismatch for {case}\nC++: {cxx}\nOrhun: {proto}",
        )
        ok_count += 1

    print(f"Parser prototype smoke passed ({ok_count} fixture ok, {error_count} fixture error).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
