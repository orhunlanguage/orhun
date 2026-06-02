#!/usr/bin/env python3
import json
import subprocess
from dataclasses import dataclass, field
from pathlib import Path


BUILTINS = {
    "aralik",
    "aralık",
    "bekle",
    "dosya",
    "gorev",
    "json",
    "metin",
    "oku",
    "regex",
    "sistem",
    "uzunluk",
    "veritabani",
    "yaz",
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
    if expression.get("tur") == "IsimsizIslev":
        return set()

    refs: set[str] = set()
    kind = expression.get("tur")
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


def is_declaration_assignment(command: dict) -> bool:
    marker = command.get("bildirim")
    require(
        isinstance(marker, bool),
        f"{command.get('tur')} at line {command.get('satir')} missing bool bildirim",
    )
    return marker


def gather_local_bindings(commands: list[dict]) -> set[str]:
    bindings: set[str] = set()
    for command in commands:
        kind = command.get("tur")
        if kind == "IslevTanim":
            name = command.get("ad")
            if isinstance(name, str) and name:
                bindings.add(name)
            continue
        if kind == "Atama" and is_declaration_assignment(command):
            name = binding_from_target(command.get("hedef"))
            if name:
                bindings.add(name)
        if kind == "CokluAtama" and is_declaration_assignment(command):
            targets = command.get("hedefler", [])
            if isinstance(targets, list):
                bindings.update(item for item in targets if isinstance(item, str))

        for block in command_blocks(command):
            bindings.update(gather_local_bindings(block))
    return bindings


def gather_refs(commands: list[dict]) -> set[str]:
    refs: set[str] = set()
    for command in commands:
        kind = command.get("tur")
        if kind == "IslevTanim":
            continue
        if kind == "Atama":
            if not is_declaration_assignment(command):
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
            refs.update(gather_refs(block))
    return refs


def gather_writes(commands: list[dict]) -> set[str]:
    writes: set[str] = set()
    for command in commands:
        kind = command.get("tur")
        if kind == "IslevTanim":
            continue
        if kind == "Atama" and not is_declaration_assignment(command):
            name = binding_from_target(command.get("hedef"))
            if name:
                writes.add(name)

        for block in command_blocks(command):
            writes.update(gather_writes(block))
    return writes


@dataclass
class FunctionInfo:
    path: str
    locals: set[str] = field(default_factory=set)
    refs: set[str] = field(default_factory=set)
    writes: set[str] = field(default_factory=set)
    captures: set[str] = field(default_factory=set)
    capture_depths: dict[str, int] = field(default_factory=dict)
    mutated_captures: set[str] = field(default_factory=set)


def analyze_callable(
    path: str,
    params: list[str],
    commands: list[dict],
    outer_scopes: list[set[str]],
    results: dict[str, FunctionInfo],
) -> None:
    locals_ = set(params)
    locals_.update(gather_local_bindings(commands))
    refs = gather_refs(commands) - BUILTINS
    writes = gather_writes(commands)
    free = refs - locals_

    captures: set[str] = set()
    capture_depths: dict[str, int] = {}
    for name in free:
        for depth, scope in enumerate(reversed(outer_scopes), start=1):
            if name in scope:
                captures.add(name)
                capture_depths[name] = depth
                break

    results[path] = FunctionInfo(
        path=path,
        locals=locals_,
        refs=refs,
        writes=writes,
        captures=captures,
        capture_depths=capture_depths,
        mutated_captures=captures & writes,
    )

    child_outer_scopes = outer_scopes + [locals_]
    analyze_nested_callables(path, commands, child_outer_scopes, results)


def params_from(node: dict) -> list[str]:
    params = node.get("parametreler", [])
    if not isinstance(params, list):
        return []
    return [item for item in params if isinstance(item, str)]


def commands_from_body(node: dict) -> list[dict]:
    body = node.get("govde")
    if not isinstance(body, dict):
        return []
    commands = body.get("komutlar", [])
    if not isinstance(commands, list):
        return []
    return [item for item in commands if isinstance(item, dict)]


def analyze_lambdas_in_expression(
    parent_path: str,
    expression: object,
    outer_scopes: list[set[str]],
    results: dict[str, FunctionInfo],
) -> None:
    if not isinstance(expression, dict):
        return
    if expression.get("tur") == "IsimsizIslev":
        line = expression.get("satir", "?")
        analyze_callable(
            f"{parent_path}.<lambda@{line}>",
            params_from(expression),
            commands_from_body(expression),
            outer_scopes,
            results,
        )
        return

    for value in expression.values():
        if isinstance(value, dict):
            analyze_lambdas_in_expression(parent_path, value, outer_scopes, results)
        elif isinstance(value, list):
            for item in value:
                analyze_lambdas_in_expression(parent_path, item, outer_scopes, results)


def analyze_nested_callables(
    parent_path: str,
    commands: list[dict],
    outer_scopes: list[set[str]],
    results: dict[str, FunctionInfo],
) -> None:
    for command in commands:
        if command.get("tur") == "IslevTanim":
            name = command.get("ad")
            if isinstance(name, str) and name:
                analyze_callable(
                    f"{parent_path}.{name}",
                    params_from(command),
                    commands_from_body(command),
                    outer_scopes,
                    results,
                )
            continue

        for value in command.values():
            if isinstance(value, dict):
                analyze_lambdas_in_expression(parent_path, value, outer_scopes, results)
            elif isinstance(value, list):
                for item in value:
                    analyze_lambdas_in_expression(parent_path, item, outer_scopes, results)

        for block in command_blocks(command):
            analyze_nested_callables(parent_path, block, outer_scopes, results)


def analyze_file(binary: Path, repo: Path, source: Path) -> dict[str, FunctionInfo]:
    ast = run_parse(binary, source, repo)
    commands = ast.get("komutlar", [])
    require(isinstance(commands, list), f"{source}: AST Program missing komutlar")
    results: dict[str, FunctionInfo] = {}
    analyze_nested_callables(
        "<program>",
        [item for item in commands if isinstance(item, dict)],
        [],
        results,
    )
    return results
