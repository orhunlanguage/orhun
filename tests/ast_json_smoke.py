#!/usr/bin/env python3
import argparse
import json
import subprocess
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def parse_last_json(text: str) -> dict:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    require(lines, "JSON output is empty")
    return json.loads(lines[-1])


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


def main() -> int:
    parser = argparse.ArgumentParser(description="Orhun parse --json smoke test")
    parser.add_argument("binary", help="Orhun executable path")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    binary = Path(args.binary)

    with tempfile.TemporaryDirectory(prefix="orhun_ast_json_") as tmp:
        tmpdir = Path(tmp)
        ok_source = tmpdir / "ast_ok.oh"
        ok_source.write_text(
            "\n".join(
                [
                    "işlev topla(a olsun 1, b olsun 2):",
                    "    döndür a + b",
                    "",
                    "sonuç olsun topla(3)",
                    "yazdır sonuç",
                    "",
                ]
            ),
            encoding="utf-8",
        )

        ok_proc = run_cmd([str(binary), "parse", str(ok_source), "--json"], repo)
        require(
            ok_proc.returncode == 0,
            f"parse --json failed:\nSTDOUT:\n{ok_proc.stdout}\nSTDERR:\n{ok_proc.stderr}",
        )
        ok_payload = parse_last_json(ok_proc.stdout)
        require(ok_payload.get("durum") == "ok", "success payload durum must be ok")
        require(ok_payload.get("hata_sayisi") == 0, "success payload must have no errors")
        ast = ok_payload.get("ast")
        require(isinstance(ast, dict), "success payload missing ast object")
        require(ast.get("tur") == "Program", "root AST node must be Program")
        komutlar = ast.get("komutlar")
        require(isinstance(komutlar, list), "Program node missing komutlar")
        require(
            [komut.get("tur") for komut in komutlar]
            == ["IslevTanim", "Atama", "Yazdir"],
            "top-level command node order changed unexpectedly",
        )
        islev = komutlar[0]
        require(islev.get("ad") == "topla", "function name not serialized")
        require(
            islev.get("parametreler") == ["a", "b"],
            "function parameters not serialized",
        )
        require(
            len(islev.get("varsayilanlar", [])) == 2,
            "default argument slots not serialized",
        )

        tree_proc = run_cmd([str(binary), "ast", str(ok_source)], repo)
        require(tree_proc.returncode == 0, "ast command should print tree")
        require("ProgramNode" in tree_proc.stdout, "ast tree output missing ProgramNode")

        bad_source = tmpdir / "ast_bad.oh"
        bad_source.write_text("eğer doğru\n", encoding="utf-8")
        bad_proc = run_cmd([str(binary), "parse", str(bad_source), "--json"], repo)
        require(bad_proc.returncode == 1, "parse --json should return rc=1 on parser errors")
        bad_payload = parse_last_json(bad_proc.stdout)
        require(bad_payload.get("durum") == "fail", "error payload durum must be fail")
        require(bad_payload.get("hata_sayisi") == 1, "error payload must report one error")
        require(bad_payload.get("ast") is None, "error payload ast must be null")
        require(
            isinstance(bad_payload.get("hata", {}).get("mesaj"), str)
            and bad_payload["hata"]["mesaj"],
            "error payload missing hata.mesaj",
        )

    print("AST JSON smoke passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
