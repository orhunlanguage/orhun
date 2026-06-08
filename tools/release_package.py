#!/usr/bin/env python3
import argparse
import gzip
import hashlib
import io
import re
import stat
import sys
import tarfile
import zipfile
from pathlib import Path, PurePosixPath


SEMVER_RE = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$"
)
PLATFORM_RE = re.compile(r"^[a-z0-9][a-z0-9._-]*$")
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def read_version(version_file: Path) -> str:
    require(version_file.is_file(), f"Version file not found: {version_file}")
    version = version_file.read_text(encoding="utf-8").strip()
    require(SEMVER_RE.fullmatch(version) is not None, f"Invalid semantic version: {version}")
    return version


def verify_tag(tag: str, version: str) -> None:
    require(tag == f"v{version}", f"Release tag {tag!r} must exactly match v{version}")


def bundle_files(bundle: Path) -> list[Path]:
    require(bundle.is_dir(), f"Bundle directory not found: {bundle}")
    require(
        (bundle / "bootstrap-compiler.manifest.json").is_file(),
        "Bundle is missing bootstrap-compiler.manifest.json",
    )
    require(
        (bundle / "StdLib" / "bootstrap.manifest.json").is_file(),
        "Bundle is missing StdLib/bootstrap.manifest.json",
    )
    require(
        (bundle / "orhun-derleyici").is_file()
        or (bundle / "orhun-derleyici.exe").is_file(),
        "Bundle is missing orhun-derleyici executable",
    )

    paths = sorted(path for path in bundle.rglob("*") if path.is_file())
    require(paths, f"Bundle is empty: {bundle}")
    require(
        not any(path.is_symlink() for path in bundle.rglob("*")),
        "Release bundles must not contain symbolic links",
    )
    return paths


def archive_mode(path: Path) -> int:
    source_mode = stat.S_IMODE(path.stat().st_mode)
    return 0o755 if source_mode & 0o111 else 0o644


def archive_name(root_name: str, bundle: Path, path: Path) -> str:
    relative = PurePosixPath(path.relative_to(bundle).as_posix())
    require(".." not in relative.parts, f"Unsafe archive path: {relative}")
    return str(PurePosixPath(root_name) / relative)


def write_zip(destination: Path, root_name: str, bundle: Path, paths: list[Path]) -> None:
    with zipfile.ZipFile(
        destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        for path in paths:
            info = zipfile.ZipInfo(archive_name(root_name, bundle, path), ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = archive_mode(path) << 16
            archive.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED)


def write_tar_gz(
    destination: Path, root_name: str, bundle: Path, paths: list[Path]
) -> None:
    with destination.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            with tarfile.open(
                fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT
            ) as archive:
                for path in paths:
                    data = path.read_bytes()
                    info = tarfile.TarInfo(archive_name(root_name, bundle, path))
                    info.size = len(data)
                    info.mode = archive_mode(path)
                    info.mtime = 0
                    info.uid = 0
                    info.gid = 0
                    info.uname = ""
                    info.gname = ""
                    archive.addfile(info, fileobj=io.BytesIO(data))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_sidecar(path: Path) -> Path:
    sidecar = path.with_name(path.name + ".sha256")
    sidecar.write_text(f"{sha256(path)}  {path.name}\n", encoding="ascii", newline="\n")
    return sidecar


def create_release(args: argparse.Namespace) -> int:
    version = read_version(args.version_file)
    verify_tag(args.tag, version)
    platform_name = args.platform.lower()
    require(PLATFORM_RE.fullmatch(platform_name) is not None, "Invalid platform name")

    bundle = args.bundle.resolve()
    paths = bundle_files(bundle)
    args.output.mkdir(parents=True, exist_ok=True)

    root_name = f"orhun-compiler-{version}-{platform_name}"
    extension = ".zip" if platform_name.startswith("windows-") else ".tar.gz"
    destination = args.output.resolve() / f"{root_name}{extension}"
    require(not destination.exists(), f"Release archive already exists: {destination}")

    if extension == ".zip":
        write_zip(destination, root_name, bundle, paths)
    else:
        write_tar_gz(destination, root_name, bundle, paths)
    sidecar = write_sidecar(destination)

    print(f"Release archive: {destination}")
    print(f"SHA-256: {sidecar}")
    return 0


def write_checksums(args: argparse.Namespace) -> int:
    directory = args.directory.resolve()
    require(directory.is_dir(), f"Release directory not found: {directory}")
    archives = sorted(
        path
        for path in directory.iterdir()
        if path.is_file() and (path.name.endswith(".zip") or path.name.endswith(".tar.gz"))
    )
    require(archives, f"No release archives found in: {directory}")

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = [f"{sha256(path)}  {path.name}" for path in archives]
    output.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    print(f"Checksum manifest: {output} ({len(archives)} archives)")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create deterministic Orhun release archives and checksums"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create", help="Create one platform release archive")
    create.add_argument("--bundle", type=Path, required=True)
    create.add_argument("--output", type=Path, required=True)
    create.add_argument("--platform", required=True)
    create.add_argument("--tag", required=True)
    create.add_argument("--version-file", type=Path, default=Path("VERSION"))
    create.set_defaults(handler=create_release)

    checksums = subparsers.add_parser(
        "checksums", help="Create a combined SHA256SUMS manifest"
    )
    checksums.add_argument("--directory", type=Path, required=True)
    checksums.add_argument("--output", type=Path, required=True)
    checksums.set_defaults(handler=write_checksums)

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    return args.handler(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"Hata: {error}", file=sys.stderr)
        raise SystemExit(1)
