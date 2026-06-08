#!/usr/bin/env python3
import hashlib
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run(repo: Path, *args: str, expected: int = 0) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(
        [sys.executable, "tools/release_package.py", *args],
        cwd=repo,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    require(
        proc.returncode == expected,
        f"Command returned {proc.returncode}, expected {expected}:\n{proc.stdout}\n{proc.stderr}",
    )
    return proc


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    version = (repo / "VERSION").read_text(encoding="utf-8").strip()

    with tempfile.TemporaryDirectory(prefix="orhun_release_smoke_") as raw_temp:
        temp = Path(raw_temp)
        bundle = temp / "bundle"
        (bundle / "StdLib" / "orhun").mkdir(parents=True)
        (bundle / "bootstrap-compiler.manifest.json").write_text(
            '{"format":"ORHUN-COMPILER-BUNDLE-1"}\n', encoding="utf-8"
        )
        (bundle / "StdLib" / "bootstrap.manifest.json").write_text(
            '{"format":"ORHUN-BOOTSTRAP-1"}\n', encoding="utf-8"
        )
        (bundle / "StdLib" / "orhun" / "derleyici.obc").write_bytes(b"ORH-OBC\x00")
        executable = bundle / "orhun-derleyici"
        executable.write_bytes(b"release-smoke")
        executable.chmod(0o755)

        outputs = []
        for attempt in ("first", "second"):
            output = temp / attempt
            run(
                repo,
                "create",
                "--bundle",
                str(bundle),
                "--output",
                str(output),
                "--platform",
                "linux-x86_64",
                "--tag",
                f"v{version}",
            )
            outputs.append(output / f"orhun-compiler-{version}-linux-x86_64.tar.gz")

        require(
            outputs[0].read_bytes() == outputs[1].read_bytes(),
            "Repeated tar.gz release archives must be byte-identical",
        )
        with tarfile.open(outputs[0], "r:gz") as archive:
            members = archive.getnames()
        require(members, "tar.gz release archive is empty")
        require(
            all(name.startswith(f"orhun-compiler-{version}-linux-x86_64/") for name in members),
            "tar.gz members must share the versioned release root",
        )

        windows_archives = []
        for attempt in ("windows-first", "windows-second"):
            windows_output = temp / attempt
            run(
                repo,
                "create",
                "--bundle",
                str(bundle),
                "--output",
                str(windows_output),
                "--platform",
                "Windows-X64",
                "--tag",
                f"v{version}",
            )
            windows_archives.append(
                windows_output / f"orhun-compiler-{version}-windows-x64.zip"
            )
        require(
            windows_archives[0].read_bytes() == windows_archives[1].read_bytes(),
            "Repeated zip release archives must be byte-identical",
        )
        windows_archive = windows_archives[0]
        with zipfile.ZipFile(windows_archive) as archive:
            members = archive.namelist()
        require(members, "zip release archive is empty")
        require(
            all(name.startswith(f"orhun-compiler-{version}-windows-x64/") for name in members),
            "zip members must share the versioned release root",
        )

        combined = temp / "combined"
        combined.mkdir()
        for archive in (outputs[0], windows_archive):
            (combined / archive.name).write_bytes(archive.read_bytes())
        checksum_manifest = combined / "SHA256SUMS"
        run(
            repo,
            "checksums",
            "--directory",
            str(combined),
            "--output",
            str(checksum_manifest),
        )
        checksum_text = checksum_manifest.read_text(encoding="ascii")
        require(
            f"{digest(outputs[0])}  {outputs[0].name}" in checksum_text,
            "Combined checksums missing tar.gz archive",
        )
        require(
            f"{digest(windows_archive)}  {windows_archive.name}" in checksum_text,
            "Combined checksums missing zip archive",
        )

        wrong_tag = subprocess.run(
            [
                sys.executable,
                "tools/release_package.py",
                "create",
                "--bundle",
                str(bundle),
                "--output",
                str(temp / "wrong-tag"),
                "--platform",
                "linux-x86_64",
                "--tag",
                "v999.0.0",
            ],
            cwd=repo,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        require(wrong_tag.returncode != 0, "Mismatched release tag must be rejected")
        require("must exactly match" in wrong_tag.stderr, "Wrong-tag error must be clear")

    print("Release packaging smoke passed (deterministic archives, checksums, tag gate).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
