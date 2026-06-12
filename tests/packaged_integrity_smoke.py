#!/usr/bin/env python3
import argparse
import hashlib
import shutil
import struct
import tempfile
import zlib
from pathlib import Path

from compiler_prototype_smoke import require, run_cmd


def combined(proc) -> str:
    return (proc.stdout + proc.stderr).replace("\r\n", "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify packaged payload integrity")
    parser.add_argument("binary", help="Orhun executable path")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    binary = Path(args.binary).resolve()
    require(binary.exists(), f"Binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="orhun_packaged_integrity_") as tmp:
        tmpdir = Path(tmp)
        source = tmpdir / "package.oh"
        source.write_text('yazdır "paket"\n', encoding="utf-8", newline="\n")
        output = tmpdir / "package_artifact"
        compiled = run_cmd([str(binary), "derle", str(source), str(output)], repo)
        require(
            compiled.returncode == 0,
            f"packaged fixture compile failed: {combined(compiled)}",
        )

        obc = output.with_suffix(".obc")
        package = output.with_suffix(".exe")
        payload = obc.read_bytes()
        package_bytes = package.read_bytes()
        trailer = package_bytes[-80:]
        payload_size, payload_crc = struct.unpack("<II", trailer[8:16])
        require(
            trailer[:8] == b"ORHNPKG2"
            and payload_size == len(payload)
            and payload_crc == zlib.crc32(payload) & 0xFFFFFFFF
            and trailer[16:].decode("ascii") == hashlib.sha256(payload).hexdigest(),
            "generated package must carry the ORHNPKG2 SHA256 trailer",
        )

        verified = run_cmd([str(binary), "paketli-dogrula", str(package)], repo)
        require(
            verified.returncode == 0 and "ORHNPKG2" in combined(verified),
            f"ORHNPKG2 verification failed: {combined(verified)}",
        )

        bad_sha_package = tmpdir / "bad-sha.exe"
        bad_sha_bytes = bytearray(package_bytes)
        bad_sha_bytes[-1] = ord("0") if bad_sha_bytes[-1] != ord("0") else ord("1")
        bad_sha_package.write_bytes(bad_sha_bytes)
        shutil.copymode(package, bad_sha_package)
        rejected_sha = run_cmd(
            [str(binary), "packaged-verify", str(bad_sha_package)],
            repo,
        )
        require(
            rejected_sha.returncode != 0
            and "SHA256 dogrulamasi basarisiz" in combined(rejected_sha),
            "packaged verifier must reject a mismatched ORHNPKG2 SHA256",
        )

        legacy_package = tmpdir / "legacy-v1.exe"
        shutil.copy2(binary, legacy_package)
        with legacy_package.open("ab") as handle:
            handle.write(payload)
            handle.write(b"ORHNPKG1")
            handle.write(struct.pack("<II", len(payload), zlib.crc32(payload)))
        verified_v1 = run_cmd(
            [str(binary), "packaged-verify", str(legacy_package)],
            repo,
        )
        require(
            verified_v1.returncode == 0 and "ORHNPKG1" in combined(verified_v1),
            f"legacy ORHNPKG1 package must remain verifiable: {combined(verified_v1)}",
        )
        legacy_run = run_cmd([str(legacy_package)], repo)
        require(
            legacy_run.returncode == 0 and combined(legacy_run).strip() == "paket",
            f"legacy ORHNPKG1 package must remain executable: {combined(legacy_run)}",
        )

    print(
        "Packaged integrity smoke passed (ORHNPKG2 SHA256, tamper rejection, "
        "ORHNPKG1 compatibility)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
