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
    return [cxx_node_summary(command) for command in commands]


def cxx_node_summary(command: dict) -> dict:
    blocks = cxx_block_summaries(command)
    expression = cxx_command_expression_summary(command)
    return {
        "tur": command.get("tur"),
        "satir": command.get("satir"),
        "ifade_turu": expression["tur"],
        "ifade_ozeti": expression,
        "blok_sayilari": block_counts(blocks),
        "bloklar": blocks,
    }


def cxx_shallow_node(command: dict) -> dict:
    expression = cxx_command_expression_summary(command)
    return {
        "tur": command.get("tur"),
        "satir": command.get("satir"),
        "ifade_turu": expression["tur"],
        "ifade_ozeti": expression,
    }


def cxx_command_expression_summary(command: dict) -> dict:
    for key in ("ifade", "kosul", "kac_kez"):
        expression = command.get(key)
        if isinstance(expression, dict):
            return cxx_expression_summary(expression)
    return empty_expression_summary()


def empty_expression_summary() -> dict:
    return {"tur": "", "op": "", "ayrinti": "", "altlar": []}


def cxx_expression_summary(expression: dict) -> dict:
    kind = expression.get("tur")
    if not isinstance(kind, str):
        return empty_expression_summary()
    return {
        "tur": kind,
        "op": expression.get("op", ""),
        "ayrinti": cxx_expression_detail(expression),
        "altlar": cxx_expression_children(expression),
    }


def cxx_expression_shallow_summary(expression: dict) -> dict:
    kind = expression.get("tur")
    if not isinstance(kind, str):
        return empty_expression_summary()
    return {
        "tur": kind,
        "op": expression.get("op", ""),
        "ayrinti": cxx_expression_detail(expression),
        "altlar": [],
    }


def cxx_expression_children(expression: dict) -> list[dict]:
    if expression.get("tur") == "IkiliIslem":
        return [
            cxx_expression_shallow_summary(child)
            for child in (expression.get("sol"), expression.get("sag"))
            if isinstance(child, dict)
        ]
    if expression.get("tur") == "Liste":
        items = expression.get("ogeler")
        if isinstance(items, list):
            return [cxx_expression_shallow_summary(item) for item in items if isinstance(item, dict)]
    if expression.get("tur") == "Sozluk":
        items = expression.get("ogeler")
        if isinstance(items, list):
            return [
                cxx_expression_shallow_summary(item.get("deger"))
                for item in items
                if isinstance(item, dict) and isinstance(item.get("deger"), dict)
            ]
    if expression.get("tur") == "ListeUretec":
        return [
            cxx_expression_shallow_summary(child)
            for child in (expression.get("ifade"), expression.get("kaynak"), expression.get("kosul"))
            if isinstance(child, dict)
        ]
    return []


def cxx_expression_detail(expression: dict) -> str:
    kind = expression.get("tur")
    if kind == "IkiliIslem":
        return str(expression.get("op", ""))
    if kind in ("Sayi", "Ondalik", "Metin"):
        return str(expression.get("deger", ""))
    if kind == "Mantik":
        value = expression.get("deger")
        if value is True:
            return "doğru"
        if value is False:
            return "yanlış"
        return ""
    if kind == "Kimlik":
        return str(expression.get("ad", ""))
    if kind in ("GuvenliAlanErisim", "BenimErisim"):
        return str(expression.get("alan", ""))
    if kind == "IndeksErisim":
        target = expression.get("hedef")
        if isinstance(target, dict):
            return str(target.get("ad", ""))
    if kind == "YeniNesne":
        return str(expression.get("sinif", ""))
    return ""


def cxx_block_summaries(command: dict) -> list[dict]:
    blocks = []
    for key in ("dogru_blok", "yanlis_blok", "govde", "deneme", "yakala"):
        block = command.get(key)
        if isinstance(block, dict):
            commands = block.get("komutlar")
            if isinstance(commands, list):
                blocks.append({"komutlar": [cxx_shallow_node(child) for child in commands]})
    return blocks


def block_counts(blocks: list[dict]) -> list[int]:
    return [len(block["komutlar"]) for block in blocks]


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
    return [orhun_node_summary(command, source_file) for command in commands]


def orhun_node_summary(command: dict, source_file: Path) -> dict:
    blocks = orhun_block_summaries(command.get("bloklar", []), source_file)
    counts = command.get("blok_sayilari", [])
    require(counts == block_counts(blocks), f"prototype block counts mismatch for {source_file}")
    expression = orhun_expression_summary(command, source_file)
    return {
        "tur": command.get("tur"),
        "satir": command.get("satir"),
        "ifade_turu": expression["tur"],
        "ifade_ozeti": expression,
        "blok_sayilari": counts,
        "bloklar": blocks,
    }


def orhun_expression_summary(command: dict, source_file: Path) -> dict:
    expression = command.get("ifade_ozeti")
    require(isinstance(expression, dict), f"prototype expression summary missing for {source_file}")
    kind = expression.get("tur", "")
    require(command.get("ifade_turu", "") == kind, f"prototype expression kind mismatch for {source_file}")
    children = expression.get("altlar", [])
    require(isinstance(children, list), f"prototype expression children invalid for {source_file}")
    return {
        "tur": kind,
        "op": expression.get("op", ""),
        "ayrinti": expression.get("ayrinti", ""),
        "altlar": [orhun_expression_payload(child, source_file) for child in children],
    }


def orhun_expression_payload(expression: object, source_file: Path) -> dict:
    require(isinstance(expression, dict), f"prototype child expression invalid for {source_file}")
    children = expression.get("altlar", [])
    require(isinstance(children, list), f"prototype child expression children invalid for {source_file}")
    return {
        "tur": expression.get("tur", ""),
        "op": expression.get("op", ""),
        "ayrinti": expression.get("ayrinti", ""),
        "altlar": children,
    }


def orhun_block_summaries(blocks: object, source_file: Path) -> list[dict]:
    require(isinstance(blocks, list), f"prototype payload has invalid bloklar for {source_file}")
    normalized = []
    for block in blocks:
        require(isinstance(block, dict), f"prototype block is not an object for {source_file}")
        commands = block.get("komutlar")
        require(isinstance(commands, list), f"prototype block missing komutlar for {source_file}")
        normalized.append(
            {
                "komutlar": [
                    orhun_shallow_node(command, source_file)
                    for command in commands
                ]
            }
        )
    return normalized


def orhun_shallow_node(command: dict, source_file: Path) -> dict:
    expression = orhun_expression_summary(command, source_file)
    return {
        "tur": command.get("tur"),
        "satir": command.get("satir"),
        "ifade_turu": expression["tur"],
        "ifade_ozeti": expression,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare Orhun parser prototype summaries against C++ AST"
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
