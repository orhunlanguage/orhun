#!/usr/bin/env python3
import argparse
import json
import subprocess
from pathlib import Path


SUCCESS_FIXTURES = (
    Path("tests/cases/basic_math.oh"),
    Path("tests/cases/boolean_logic.oh"),
    Path("tests/cases/function_return.oh"),
)
ERROR_FIXTURE = Path("tests/ast_json/error_missing_ise.oh")
CONSTANT_KINDS = {"bos", "sayi", "metin", "mantik"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def parse_last_json(text: str) -> dict:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    require(lines, "Bytecode JSON output is empty")
    payload = json.loads(lines[-1])
    require(isinstance(payload, dict), "Bytecode JSON root must be an object")
    return payload


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


def validate_success(payload: dict, source: Path) -> None:
    require(payload.get("durum") == "ok", f"{source}: durum must be ok")
    require(payload.get("hata_sayisi") == 0, f"{source}: hata_sayisi must be 0")

    bytecode = payload.get("bytecode")
    require(isinstance(bytecode, dict), f"{source}: bytecode object missing")
    commands = bytecode.get("komutlar")
    constants = bytecode.get("sabitler")
    require(isinstance(commands, list) and commands, f"{source}: commands missing")
    require(isinstance(constants, list), f"{source}: constants missing")
    require(
        bytecode.get("komut_sayisi") == len(commands),
        f"{source}: komut_sayisi does not match command list",
    )
    require(
        bytecode.get("sabit_sayisi") == len(constants),
        f"{source}: sabit_sayisi does not match constant list",
    )
    require(
        isinstance(bytecode.get("kod_boyutu"), int)
        and bytecode["kod_boyutu"] > commands[-1].get("ip", -1),
        f"{source}: kod_boyutu must cover the final instruction",
    )

    previous_ip = -1
    function_count = 0
    for command in commands:
        require(isinstance(command, dict), f"{source}: command must be an object")
        ip = command.get("ip")
        require(isinstance(ip, int) and ip > previous_ip, f"{source}: invalid ip")
        require(
            isinstance(command.get("op"), str)
            and command["op"].startswith("OP_")
            and command["op"] != "OP_BILINMEYEN",
            f"{source}: invalid opcode at ip {ip}",
        )
        require(
            isinstance(command.get("satir"), int) and command["satir"] >= 1,
            f"{source}: invalid source line at ip {ip}",
        )
        if "operand" in command:
            require(
                isinstance(command["operand"], int) and command["operand"] >= 0,
                f"{source}: invalid operand at ip {ip}",
            )
        if command["op"] == "OP_ISLEV_OLUSTUR":
            function_count += 1
            function = command.get("islev")
            require(isinstance(function, dict), f"{source}: function metadata missing")
            for field in (
                "ad_sabit",
                "min_arity",
                "max_arity",
                "giris",
                "local_sayisi",
                "baglam_arg",
            ):
                require(
                    isinstance(function.get(field), int) and function[field] >= 0,
                    f"{source}: invalid function field {field}",
                )
            require(
                isinstance(function.get("ad"), str) and function["ad"],
                f"{source}: function name missing",
            )
            local_names = function.get("local_adlari")
            require(
                isinstance(local_names, list),
                f"{source}: function local names missing",
            )
            require(
                all(
                    isinstance(item, dict)
                    and isinstance(item.get("indeks"), int)
                    and isinstance(item.get("ad_sabit"), int)
                    and isinstance(item.get("ad"), str)
                    for item in local_names
                ),
                f"{source}: invalid function local name metadata",
            )
        previous_ip = ip

    require(commands[-2]["op"] == "OP_BOS", f"{source}: program must end with OP_BOS")
    require(commands[-1]["op"] == "OP_DON", f"{source}: program must end with OP_DON")

    for constant in constants:
        require(isinstance(constant, dict), f"{source}: constant must be an object")
        require(
            constant.get("tur") in CONSTANT_KINDS,
            f"{source}: unknown constant kind {constant.get('tur')}",
        )
        if constant["tur"] != "bos":
            require("deger" in constant, f"{source}: constant value missing")

    if source.name == "function_return.oh":
        require(function_count > 0, f"{source}: function metadata was not exercised")


def validate_error(payload: dict, source: Path) -> None:
    require(payload.get("durum") == "fail", f"{source}: durum must be fail")
    require(payload.get("hata_sayisi") == 1, f"{source}: hata_sayisi must be 1")
    require(payload.get("bytecode") is None, f"{source}: bytecode must be null")
    require(
        isinstance(payload.get("hata", {}).get("mesaj"), str)
        and payload["hata"]["mesaj"],
        f"{source}: error message missing",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Orhun bytecode JSON contract smoke test")
    parser.add_argument("binary", help="Orhun executable path")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    binary = Path(args.binary).resolve()
    require(binary.exists(), f"Binary not found: {binary}")

    for source in SUCCESS_FIXTURES:
        proc = run_cmd([str(binary), "baytkod", str(source), "--json"], repo)
        require(
            proc.returncode == 0,
            f"{source}: bytecode command failed\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}",
        )
        validate_success(parse_last_json(proc.stdout), source)

    proc = run_cmd([str(binary), "bytecode", str(ERROR_FIXTURE), "--json"], repo)
    require(proc.returncode == 1, f"{ERROR_FIXTURE}: error case must return rc=1")
    validate_error(parse_last_json(proc.stdout), ERROR_FIXTURE)

    print(
        f"Bytecode JSON smoke passed ({len(SUCCESS_FIXTURES)} ok, 1 error fixture)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
