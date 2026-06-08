#!/usr/bin/env python3
import argparse
import json
import os
import tempfile
import zlib
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

        obc_stdlib = tmpdir / "obc_stdlib"
        obc_orhun = obc_stdlib / "orhun"
        prepare_env = os.environ.copy()
        prepare_env["ORHUN_MODULE_MODE"] = "obc-only"
        prepare = run_cmd(
            [str(binary), "bootstrap-hazirla", str(obc_stdlib)],
            repo,
            env=prepare_env,
        )
        require(
            prepare.returncode == 0,
            f"bootstrap prepare failed: {combined(prepare)}",
        )
        manifest_path = obc_stdlib / "bootstrap.manifest.json"
        require(manifest_path.exists(), "bootstrap prepare manifest missing")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        require(
            manifest.get("format") == "orhun-bootstrap-v1"
            and manifest.get("module_mode") == "obc-only",
            "bootstrap prepare manifest contract mismatch",
        )
        modules = manifest.get("modules")
        require(
            isinstance(modules, list) and len(modules) == 4,
            "bootstrap prepare manifest must list four modules",
        )
        manifest_modules = {
            entry.get("module"): entry for entry in modules if isinstance(entry, dict)
        }
        for module in ("lexer", "parser", "derleyici", "derleyici_cli"):
            module_name = f"orhun/{module}.oh"
            artifact = obc_orhun / f"{module}.obc"
            require(
                artifact.exists(),
                f"bootstrap prepare artifact missing for {module}",
            )
            require(
                not (obc_orhun / f"{module}.oh").exists(),
                f"bootstrap prepare must remain source-free for {module}",
            )
            entry = manifest_modules.get(module_name)
            require(isinstance(entry, dict), f"manifest entry missing for {module}")
            payload = artifact.read_bytes()
            require(
                entry.get("payload_size") == len(payload),
                f"manifest payload size mismatch for {module}",
            )
            require(
                entry.get("payload_crc32") == f"{zlib.crc32(payload) & 0xFFFFFFFF:08x}",
                f"manifest CRC mismatch for {module}",
            )

        valid_verify = run_cmd(
            [str(binary), "bootstrap-dogrula", str(obc_stdlib)],
            repo,
        )
        require(
            valid_verify.returncode == 0,
            f"bootstrap verification failed: {combined(valid_verify)}",
        )
        require(
            "Bootstrap toolchain dogrulandi:" in combined(valid_verify),
            "bootstrap verification must report success",
        )

        original_manifest_text = manifest_path.read_text(encoding="utf-8")
        bad_crc_manifest = json.loads(original_manifest_text)
        bad_crc_manifest["modules"][0]["payload_crc32"] = "00000000"
        manifest_path.write_text(
            json.dumps(bad_crc_manifest, ensure_ascii=False),
            encoding="utf-8",
            newline="\n",
        )
        bad_crc_verify = run_cmd(
            [str(binary), "bootstrap-verify", str(obc_stdlib)],
            repo,
        )
        require(
            bad_crc_verify.returncode != 0,
            "bootstrap verification must reject manifest CRC mismatch",
        )
        require(
            "payload CRC32 uyusmuyor" in combined(bad_crc_verify),
            "bootstrap CRC rejection must explain the mismatch",
        )
        manifest_path.write_text(
            original_manifest_text,
            encoding="utf-8",
            newline="\n",
        )

        parser_artifact = obc_orhun / "parser.obc"
        original_parser_payload = parser_artifact.read_bytes()
        invalid_parser_payload = bytearray(original_parser_payload)
        invalid_parser_payload[0] ^= 0xFF
        parser_artifact.write_bytes(invalid_parser_payload)
        bad_obc_manifest = json.loads(original_manifest_text)
        parser_entry = next(
            entry
            for entry in bad_obc_manifest["modules"]
            if entry["module"] == "orhun/parser.oh"
        )
        parser_entry["payload_crc32"] = (
            f"{zlib.crc32(invalid_parser_payload) & 0xFFFFFFFF:08x}"
        )
        manifest_path.write_text(
            json.dumps(bad_obc_manifest, ensure_ascii=False),
            encoding="utf-8",
            newline="\n",
        )
        bad_obc_verify = run_cmd(
            [str(binary), "bootstrap-dogrula", str(obc_stdlib)],
            repo,
        )
        require(
            bad_obc_verify.returncode != 0,
            "bootstrap verification must reject invalid OBC payload",
        )
        require(
            "bytecode gecersiz" in combined(bad_obc_verify),
            "bootstrap OBC rejection must explain the invalid bytecode",
        )
        parser_artifact.write_bytes(original_parser_payload)
        manifest_path.write_text(
            original_manifest_text,
            encoding="utf-8",
            newline="\n",
        )

        obc_only_env = os.environ.copy()
        obc_only_env["ORHUN_STDLIB_PATH"] = str(obc_stdlib)
        obc_only = run_cmd(
            [str(binary), "orhun-vm", str(artifact_source), "--obc-only"],
            repo,
            env=obc_only_env,
        )
        require(
            obc_only.returncode == 0,
            f"obc-only compiler chain failed: {combined(obc_only)}",
        )
        require(
            combined(obc_only) == combined(artifact_direct),
            "source-free obc-only compiler chain must match direct VM output",
        )

        strict_base = tmpdir / "strict_artifact"
        strict_compile = run_cmd(
            [
                str(binary),
                "orhun-derle",
                str(artifact_source),
                "--obc-only",
                str(strict_base),
            ],
            repo,
            env=obc_only_env,
        )
        require(
            strict_compile.returncode == 0,
            f"source-free strict compile failed: {combined(strict_compile)}",
        )
        require(
            strict_base.with_suffix(".obc").read_bytes()
            == cxx_base.with_suffix(".obc").read_bytes(),
            "source-free strict compile artifact must match C++ compiler",
        )

        standalone_base = tmpdir / "standalone_artifact"
        standalone_compile = run_cmd(
            [
                str(binary),
                "bootstrap-derle",
                str(obc_stdlib),
                str(artifact_source),
                str(standalone_base),
            ],
            repo,
        )
        require(
            standalone_compile.returncode == 0,
            f"standalone bootstrap compile failed: {combined(standalone_compile)}",
        )
        require(
            standalone_base.with_suffix(".obc").read_bytes()
            == cxx_base.with_suffix(".obc").read_bytes(),
            "standalone bootstrap compile artifact must match C++ compiler",
        )

        standalone_run = run_cmd(
            [
                str(binary),
                "bootstrap-calistir",
                str(obc_stdlib),
                str(artifact_source),
            ],
            repo,
        )
        require(
            standalone_run.returncode == 0,
            f"standalone bootstrap run failed: {combined(standalone_run)}",
        )
        require(
            combined(standalone_run) == combined(artifact_direct),
            "standalone bootstrap run output must match direct VM output",
        )

        compiler_bundle = tmpdir / "compiler_bundle"
        bundle_create = run_cmd(
            [
                str(binary),
                "bootstrap-derleyici-paketle",
                str(obc_stdlib),
                str(compiler_bundle),
            ],
            repo,
        )
        require(
            bundle_create.returncode == 0,
            f"bootstrap compiler bundle failed: {combined(bundle_create)}",
        )
        bundle_exe = compiler_bundle / (
            "orhun-derleyici.exe" if os.name == "nt" else "orhun-derleyici"
        )
        require(bundle_exe.exists(), "bootstrap compiler executable missing")
        require(
            (compiler_bundle / "bootstrap-compiler.manifest.json").exists(),
            "bootstrap compiler bundle manifest missing",
        )
        require(
            not list((compiler_bundle / "StdLib").rglob("*.oh")),
            "bootstrap compiler bundle must remain source-free",
        )

        bundled_compile = run_cmd(
            [str(bundle_exe), str(artifact_source)],
            tmpdir,
        )
        require(
            bundled_compile.returncode == 0,
            f"bundled Orhun compiler failed: {combined(bundled_compile)}",
        )
        bundled_payload = json.loads(bundled_compile.stdout)
        require(
            bundled_payload.get("durum") == "ok"
            and bundled_payload.get("hata_sayisi") == 0,
            f"bundled Orhun compiler payload failed: {bundled_payload}",
        )
        bundled_json = tmpdir / "bundled.bytecode.json"
        bundled_json.write_text(
            json.dumps(bundled_payload, ensure_ascii=False),
            encoding="utf-8",
            newline="\n",
        )
        bundled_run = run_cmd(
            [str(binary), "baytkod-yurut", str(bundled_json)],
            repo,
        )
        require(
            bundled_run.returncode == 0,
            f"bundled compiler bytecode failed: {combined(bundled_run)}",
        )
        require(
            combined(bundled_run) == combined(artifact_direct),
            "bundled compiler output must execute like direct VM output",
        )

        bundled_artifact = tmpdir / "bundled_direct_artifact"
        bundled_direct_compile = run_cmd(
            [
                str(bundle_exe),
                "--derle",
                str(artifact_source),
                str(bundled_artifact),
            ],
            tmpdir,
        )
        require(
            bundled_direct_compile.returncode == 0,
            "bundled compiler direct artifact mode failed: "
            + combined(bundled_direct_compile),
        )
        require(
            bundled_artifact.with_suffix(".obc").read_bytes()
            == cxx_base.with_suffix(".obc").read_bytes(),
            "bundled compiler direct OBC must match C++ compiler artifact",
        )
        bundled_direct_exe = run_cmd(
            [str(bundled_artifact.with_suffix(".exe"))],
            tmpdir,
        )
        require(
            bundled_direct_exe.returncode == 0,
            f"bundled compiler packaged artifact failed: {combined(bundled_direct_exe)}",
        )
        require(
            combined(bundled_direct_exe) == combined(artifact_direct),
            "bundled compiler packaged artifact must match direct VM output",
        )

        bundle_parser = compiler_bundle / "StdLib" / "orhun" / "parser.obc"
        bundle_parser.write_bytes(bundle_parser.read_bytes()[:-1])
        corrupt_bundle = run_cmd(
            [str(bundle_exe), str(artifact_source)],
            tmpdir,
        )
        require(
            corrupt_bundle.returncode != 0,
            "bundled compiler must reject corrupt sibling toolchain",
        )
        require(
            "bootstrap manifesti gecersiz" in combined(corrupt_bundle),
            "bundled compiler corruption error must explain toolchain failure",
        )

        (obc_orhun / "parser.obc").unlink()
        missing_verify = run_cmd(
            [str(binary), "bootstrap-dogrula", str(obc_stdlib)],
            repo,
        )
        require(
            missing_verify.returncode != 0,
            "bootstrap verification must reject incomplete toolchains",
        )
        require(
            "bootstrap toolchain modulu bulunamadi" in combined(missing_verify),
            "bootstrap verification missing-module error must explain the issue",
        )

        missing_standalone = run_cmd(
            [
                str(binary),
                "bootstrap-derle",
                str(obc_stdlib),
                str(artifact_source),
                str(tmpdir / "missing_standalone"),
            ],
            repo,
        )
        require(
            missing_standalone.returncode != 0,
            "bootstrap-derle must reject incomplete toolchains",
        )
        require(
            "bootstrap toolchain modulu bulunamadi" in combined(missing_standalone),
            "bootstrap-derle missing-module error must explain the toolchain issue",
        )

        missing_obc = run_cmd(
            [str(binary), "orhun-vm", str(artifact_source), "--obc-only"],
            repo,
            env=obc_only_env,
        )
        require(missing_obc.returncode != 0, "obc-only must reject missing modules")
        require(
            "obc-only modunda onceden derlenmis modul bulunamadi"
            in combined(missing_obc),
            "obc-only missing-module error must explain the strict policy",
        )

        source_override_env = os.environ.copy()
        source_override_env["ORHUN_MODULE_MODE"] = "obc-only"
        source_override = run_cmd(
            [str(binary), "orhun-vm", str(artifact_source), "--source"],
            repo,
            env=source_override_env,
        )
        require(
            source_override.returncode == 0,
            f"--source must override environment policy: {combined(source_override)}",
        )
        require(
            combined(source_override) == combined(artifact_direct),
            "--source override output must match direct VM output",
        )

        invalid_mode = run_cmd(
            [str(binary), "orhun-vm", str(artifact_source), "--modul-modu=yanlis"],
            repo,
        )
        require(invalid_mode.returncode != 0, "invalid module mode must fail")
        require(
            "--modul-modu source, obc-first veya obc-only olmali"
            in combined(invalid_mode),
            "invalid module mode error must list accepted policies",
        )

    print(
        f"Compiler bootstrap smoke passed ({len(FIXTURES)} bridge and "
        f"{len(FIXTURES)} orhun-vm parity, "
        "1 artifact parity, 1 self-source artifact parity, "
        "1 prepared obc-only module chain, 1 source-free strict compile, "
        "1 standalone bootstrap compile, 1 standalone bootstrap run, "
        "1 standalone bootstrap verification, 1 source-free compiler bundle, "
        "1 bundled direct artifact compile, 1 source override, "
        "8 rejected invalid inputs)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
