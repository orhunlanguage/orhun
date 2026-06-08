#!/usr/bin/env python3
import argparse
import json
import tempfile
from pathlib import Path

from compiler_prototype_smoke import prototype_payload, require, run_cmd


FIXTURES = (
    "f_string.oh",
    "function_returned_named.oh",
    "oop_super.oh",
    "vm_try_catch.oh",
)


def combined(proc) -> str:
    return (proc.stdout + proc.stderr).replace("\r\n", "\n")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Execute Orhun-written compiler bytecode JSON through the C++ VM"
    )
    parser.add_argument("binary", help="Orhun executable path")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    binary = Path(args.binary).resolve()
    require(binary.exists(), f"Binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="orhun_bootstrap_") as tmp:
        tmpdir = Path(tmp)
        for fixture in FIXTURES:
            source = repo / "tests" / "cases" / fixture
            require(source.exists(), f"bootstrap fixture not found: {fixture}")

            payload = prototype_payload(binary, repo, source, tmpdir)
            require(
                payload.get("durum") == "ok" and payload.get("hata_sayisi") == 0,
                f"Orhun compiler failed for {fixture}: {payload}",
            )
            payload_path = tmpdir / f"{source.stem}.bytecode.json"
            serialized = payload["bytecode"] if fixture == FIXTURES[0] else payload
            payload_path.write_text(
                json.dumps(serialized, ensure_ascii=False, separators=(",", ":")),
                encoding="utf-8",
                newline="\n",
            )

            bridge = run_cmd(
                [str(binary), "baytkod-yurut", str(payload_path)],
                repo,
            )
            direct = run_cmd([str(binary), "vm-kati", str(source)], repo)
            require(
                bridge.returncode == 0,
                f"bootstrap bridge failed for {fixture}: {combined(bridge)}",
            )
            require(
                direct.returncode == 0,
                f"direct VM failed for {fixture}: {combined(direct)}",
            )
            require(
                combined(bridge) == combined(direct),
                f"bootstrap output mismatch for {fixture}\n"
                f"bridge={combined(bridge)!r}\ndirect={combined(direct)!r}",
            )
            command = "bootstrap-vm" if fixture == FIXTURES[0] else "orhun-vm"
            single_command = run_cmd([str(binary), command, str(source)], repo)
            require(
                single_command.returncode == 0,
                f"orhun-vm failed for {fixture}: {combined(single_command)}",
            )
            require(
                combined(single_command) == combined(direct),
                f"orhun-vm output mismatch for {fixture}\n"
                f"orhun-vm={combined(single_command)!r}\ndirect={combined(direct)!r}",
            )

        invalid = tmpdir / "invalid.bytecode.json"
        invalid.write_text(
            json.dumps(
                {
                    "kod_boyutu": 1,
                    "komut_sayisi": 1,
                    "sabit_sayisi": 0,
                    "komutlar": [{"ip": 0, "op": "OP_BILINMEYEN", "satir": 1}],
                    "sabitler": [],
                },
                separators=(",", ":"),
            ),
            encoding="utf-8",
            newline="\n",
        )
        rejected = run_cmd([str(binary), "bytecode-run", str(invalid)], repo)
        require(rejected.returncode != 0, "invalid bytecode JSON must be rejected")
        require(
            "bilinmeyen opcode" in combined(rejected),
            "invalid bytecode JSON rejection must explain the opcode error",
        )

        artifact_source = repo / "tests" / "cases" / "function_returned_named.oh"
        orhun_base = tmpdir / "orhun_artifact"
        cxx_base = tmpdir / "cxx_artifact"
        orhun_compile = run_cmd(
            [str(binary), "orhun-derle", str(artifact_source), str(orhun_base)],
            repo,
        )
        cxx_compile = run_cmd(
            [str(binary), "derle", str(artifact_source), str(cxx_base)],
            repo,
        )
        require(
            orhun_compile.returncode == 0,
            f"orhun-derle failed: {combined(orhun_compile)}",
        )
        require(
            cxx_compile.returncode == 0,
            f"C++ derle failed: {combined(cxx_compile)}",
        )
        for suffix in (".obc", ".obc.meta.json"):
            orhun_artifact = orhun_base.with_suffix(suffix)
            cxx_artifact = cxx_base.with_suffix(suffix)
            require(orhun_artifact.exists(), f"missing artifact: {orhun_artifact}")
            require(cxx_artifact.exists(), f"missing artifact: {cxx_artifact}")
            require(
                orhun_artifact.read_bytes() == cxx_artifact.read_bytes(),
                f"artifact mismatch for {suffix}",
            )

        artifact_obc = run_cmd(
            [str(binary), "obc", str(orhun_base.with_suffix(".obc"))],
            repo,
        )
        artifact_exe = run_cmd([str(orhun_base.with_suffix(".exe"))], repo)
        artifact_direct = run_cmd(
            [str(binary), "vm-kati", str(artifact_source)],
            repo,
        )
        require(
            artifact_obc.returncode == 0,
            f"artifact OBC failed: {combined(artifact_obc)}",
        )
        require(
            artifact_exe.returncode == 0,
            f"artifact exe failed: {combined(artifact_exe)}",
        )
        require(
            combined(artifact_obc) == combined(artifact_direct)
            and combined(artifact_exe) == combined(artifact_direct),
            "orhun-derle artifacts must match direct VM output",
        )

        compiler_source = repo / "StdLib" / "orhun" / "derleyici.oh"
        self_orhun_base = tmpdir / "self_orhun_compiler"
        self_cxx_base = tmpdir / "self_cxx_compiler"
        self_orhun = run_cmd(
            [
                str(binary),
                "orhun-derle",
                str(compiler_source),
                str(self_orhun_base),
            ],
            repo,
        )
        self_cxx = run_cmd(
            [str(binary), "derle", str(compiler_source), str(self_cxx_base)],
            repo,
        )
        require(
            self_orhun.returncode == 0,
            f"Orhun compiler self-source compile failed: {combined(self_orhun)}",
        )
        require(
            self_cxx.returncode == 0,
            f"C++ compiler self-source compile failed: {combined(self_cxx)}",
        )
        require(
            self_orhun_base.with_suffix(".obc").read_bytes()
            == self_cxx_base.with_suffix(".obc").read_bytes(),
            "Orhun compiler self-source OBC must match C++ compiler artifact",
        )

    print(
        f"Compiler bootstrap smoke passed ({len(FIXTURES)} bridge and "
        f"{len(FIXTURES)} orhun-vm parity, "
        "1 artifact parity, 1 self-source artifact parity, "
        "1 rejected invalid payload)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
