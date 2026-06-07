#!/usr/bin/env python3
import argparse
import json
import subprocess
import tempfile
from pathlib import Path


PARITY_CASES = {
    "number": "yazdır 5\n",
    "string": 'yazdır "merhaba"\n',
    "booleans": "yazdır doğru\nyazdır yanlış\n",
    "global_string": 'ad olsun "Orhun"\nyazdır ad\n',
    "global_binary": "a olsun 2\nb olsun 3\nyazdır a + b\n",
    "mixed_binary": "a olsun 7\nyazdır a - 2\nyazdır a eşit 7\n",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def parse_last_json(text: str) -> dict:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    require(lines, "JSON output is empty")
    payload = json.loads(lines[-1])
    require(isinstance(payload, dict), "JSON root must be an object")
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


def orhun_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def cxx_bytecode(binary: Path, repo: Path, source: Path) -> dict:
    proc = run_cmd([str(binary), "baytkod", str(source), "--json"], repo)
    require(
        proc.returncode == 0,
        f"C++ compiler failed for {source.name}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}",
    )
    payload = parse_last_json(proc.stdout)
    require(payload.get("durum") == "ok", f"C++ payload failed for {source.name}")
    bytecode = payload.get("bytecode")
    require(isinstance(bytecode, dict), f"C++ bytecode missing for {source.name}")
    return bytecode


def prototype_payload(binary: Path, repo: Path, source: Path, tmpdir: Path) -> dict:
    driver = tmpdir / f"{source.stem}_driver.oh"
    source_path = source.resolve().as_posix()
    driver.write_text(
        'derleyici olsun dahil_et "orhun/derleyici.oh"\n'
        f'kaynak olsun dosya.oku("{orhun_string(source_path)}")\n'
        "yazdır json.yaz(derleyici.derle(kaynak))\n",
        encoding="utf-8",
        newline="\n",
    )
    proc = run_cmd([str(binary), str(driver)], repo)
    require(
        proc.returncode == 0,
        f"Orhun compiler prototype failed for {source.name}\n"
        f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}",
    )
    return parse_last_json(proc.stdout)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare Orhun compiler prototype output against C++ bytecode"
    )
    parser.add_argument("binary", help="Orhun executable path")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    binary = Path(args.binary).resolve()
    require(binary.exists(), f"Binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="orhun_compiler_proto_") as tmp:
        tmpdir = Path(tmp)
        for name, source_text in PARITY_CASES.items():
            source = tmpdir / f"{name}.oh"
            source.write_text(source_text, encoding="utf-8", newline="\n")
            expected = cxx_bytecode(binary, repo, source)
            payload = prototype_payload(binary, repo, source, tmpdir)
            require(
                payload.get("durum") == "ok" and payload.get("hata_sayisi") == 0,
                f"prototype returned error for {name}: {payload}",
            )
            require(
                payload.get("bytecode") == expected,
                f"Compiler prototype bytecode mismatch for {name}\n"
                f"C++: {expected}\nOrhun: {payload.get('bytecode')}",
            )

        unsupported = tmpdir / "unsupported.oh"
        unsupported.write_text(
            'tekrarla 2 kez:\n    yazdır "a"\n',
            encoding="utf-8",
            newline="\n",
        )
        payload = prototype_payload(binary, repo, unsupported, tmpdir)
        require(payload.get("durum") == "fail", "unsupported command must fail")
        require(payload.get("hata_sayisi") == 1, "unsupported command error count")
        require(
            "Desteklenmeyen" in payload.get("hata", {}).get("mesaj", ""),
            "unsupported command must explain the limitation",
        )

    print(
        f"Compiler prototype smoke passed ({len(PARITY_CASES)} parity, "
        "1 unsupported fixture)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
