#!/usr/bin/env python3
import argparse
import json
import subprocess
from dataclasses import dataclass, field
from pathlib import Path


BUILTINS = {
    "bekle",
    "dosya",
    "gorev",
    "json",
    "metin",
    "regex",
    "sistem",
    "uzunluk",
    "veritabani",
    "yazdır",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def parse_last_json(text: str) -> dict:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    require(lines, "parse --json output is empty")
    return json.loads(lines[-1])


def run_parse(binary: Path, source: Path, repo: Path) -> dict:
    proc = subprocess.run(
        [str(binary), "parse", str(source), "--json"],
        cwd=repo,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    require(
        proc.returncode == 0,
        f"parse --json failed for {source}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}",
    )
    payload = parse_last_json(proc.stdout)
    require(payload.get("durum") == "ok", f"parse payload is not ok: {payload}")
    ast = payload.get("ast")
    require(isinstance(ast, dict), "parse payload missing ast object")
    return ast


def source_line(lines: list[str], line_no: int) -> str:
    if line_no <= 0 or line_no > len(lines):
        return ""
    return lines[line_no - 1]


def is_declaration_assignment(command: dict, lines: list[str]) -> bool:
    return " olsun " in source_line(lines, int(command.get("satir", 0)))


def binding_from_target(target: object) -> str | None:
    if not isinstance(target, dict):
        return None
    if target.get("tur") == "Kimlik":
        name = target.get("ad")
        return name if isinstance(name, str) and name else None
    return None


def call_base(name: object) -> str | None:
    if not isinstance(name, str) or not name:
        return None
    return name.split(".", 1)[0]


def expression_refs(expression: object) -> set[str]:
    if not isinstance(expression, dict):
        return set()

    kind = expression.get("tur")
    refs: set[str] = set()
    if kind == "Kimlik":
        name = expression.get("ad")
        if isinstance(name, str) and name:
            refs.add(name)
    elif kind == "IslevCagri":
        base = call_base(expression.get("ad"))
        if base:
            refs.add(base)

    for key, value in expression.items():
        if key in {"tur", "satir", "ad", "deger", "op", "parametreler"}:
            continue
        if isinstance(value, dict):
            refs.update(expression_refs(value))
        elif isinstance(value, list):
            for item in value:
                refs.update(expression_refs(item))
    return refs


def command_blocks(command: dict) -> list[list[dict]]:
    blocks: list[list[dict]] = []
    for key in ("dogru_blok", "yanlis_blok", "govde", "deneme", "yakala"):
        block = command.get(key)
        if isinstance(block, dict):
            commands = block.get("komutlar")
            if isinstance(commands, list):
                blocks.append([item for item in commands if isinstance(item, dict)])
    return blocks


def gather_local_bindings(commands: list[dict], lines: list[str]) -> set[str]:
    bindings: set[str] = set()
    for command in commands:
        kind = command.get("tur")
        if kind == "IslevTanim":
            name = command.get("ad")
            if isinstance(name, str) and name:
                bindings.add(name)
            continue
        if kind == "Atama" and is_declaration_assignment(command, lines):
            name = binding_from_target(command.get("hedef"))
            if name:
                bindings.add(name)
        if kind == "CokluAtama" and is_declaration_assignment(command, lines):
            targets = command.get("hedefler", [])
            if isinstance(targets, list):
                bindings.update(item for item in targets if isinstance(item, str))

        for block in command_blocks(command):
            bindings.update(gather_local_bindings(block, lines))
    return bindings


def gather_refs(commands: list[dict], lines: list[str]) -> set[str]:
    refs: set[str] = set()
    for command in commands:
        kind = command.get("tur")
        if kind == "IslevTanim":
            continue
        if kind == "Atama":
            if not is_declaration_assignment(command, lines):
                name = binding_from_target(command.get("hedef"))
                if name:
                    refs.add(name)
            refs.update(expression_refs(command.get("ifade")))
        else:
            for key, value in command.items():
                if key in {"tur", "satir", "hedef", "hedefler"}:
                    continue
                if isinstance(value, dict):
                    refs.update(expression_refs(value))
                elif isinstance(value, list):
                    for item in value:
                        refs.update(expression_refs(item))

        for block in command_blocks(command):
            refs.update(gather_refs(block, lines))
    return refs


@dataclass
class FunctionInfo:
    path: str
    locals: set[str] = field(default_factory=set)
    refs: set[str] = field(default_factory=set)
    captures: set[str] = field(default_factory=set)


def analyze_function(
    path: str,
    params: list[str],
    commands: list[dict],
    outer_scopes: list[set[str]],
    lines: list[str],
    results: dict[str, FunctionInfo],
) -> None:
    locals_ = set(params)
    locals_.update(gather_local_bindings(commands, lines))
    refs = gather_refs(commands, lines) - BUILTINS
    free = refs - locals_

    captures: set[str] = set()
    for name in free:
        for scope in reversed(outer_scopes):
            if name in scope:
                captures.add(name)
                break

    results[path] = FunctionInfo(path=path, locals=locals_, refs=refs, captures=captures)

    child_outer_scopes = outer_scopes + [locals_]
    for command in commands:
        if command.get("tur") == "IslevTanim":
            name = command.get("ad")
            body = command.get("govde")
            params_raw = command.get("parametreler", [])
            if isinstance(name, str) and isinstance(body, dict) and isinstance(params_raw, list):
                child_commands = body.get("komutlar", [])
                if isinstance(child_commands, list):
                    analyze_function(
                        f"{path}.{name}",
                        [item for item in params_raw if isinstance(item, str)],
                        [item for item in child_commands if isinstance(item, dict)],
                        child_outer_scopes,
                        lines,
                        results,
                    )
            continue

        for block in command_blocks(command):
            analyze_nested_functions(path, block, child_outer_scopes, lines, results)


def analyze_nested_functions(
    parent_path: str,
    commands: list[dict],
    outer_scopes: list[set[str]],
    lines: list[str],
    results: dict[str, FunctionInfo],
) -> None:
    for command in commands:
        if command.get("tur") == "IslevTanim":
            name = command.get("ad")
            body = command.get("govde")
            params_raw = command.get("parametreler", [])
            if isinstance(name, str) and isinstance(body, dict) and isinstance(params_raw, list):
                child_commands = body.get("komutlar", [])
                if isinstance(child_commands, list):
                    analyze_function(
                        f"{parent_path}.{name}",
                        [item for item in params_raw if isinstance(item, str)],
                        [item for item in child_commands if isinstance(item, dict)],
                        outer_scopes,
                        lines,
                        results,
                    )
            continue
        for block in command_blocks(command):
            analyze_nested_functions(parent_path, block, outer_scopes, lines, results)


def main() -> int:
    parser = argparse.ArgumentParser(description="Static closure capture analysis smoke")
    parser.add_argument("binary", help="Orhun executable")
    args = parser.parse_args()

    repo = Path.cwd()
    binary = Path(args.binary)
    require(binary.exists(), f"Binary not found: {binary}")

    source = repo / "tests" / "cases" / "closure_missing_feature.oh"
    ast = run_parse(binary, source, repo)
    lines = source.read_text(encoding="utf-8").splitlines()
    commands = ast.get("komutlar", [])
    require(isinstance(commands, list), "AST Program missing komutlar")

    results: dict[str, FunctionInfo] = {}
    analyze_nested_functions(
        "<program>",
        [item for item in commands if isinstance(item, dict)],
        [],
        lines,
        results,
    )

    expected = {
        "<program>.sayac_uret.artir": {"adet"},
        "<program>.dis_fonksiyon.ic_fonksiyon": {"dis_x", "param"},
        "<program>.banka_hesabi.para_yatir": {"bakiye"},
        "<program>.banka_hesabi.para_cek": {"bakiye"},
        "<program>.fonksiyon_listesi_uret.yazdir_func": {"x"},
    }

    for path, captures in expected.items():
        require(path in results, f"Closure analysis missing function path: {path}")
        require(
            results[path].captures == captures,
            f"{path} captures changed: expected={sorted(captures)} "
            f"actual={sorted(results[path].captures)} refs={sorted(results[path].refs)} "
            f"locals={sorted(results[path].locals)}",
        )

    print(f"Closure capture analysis smoke passed ({len(expected)} closures).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
