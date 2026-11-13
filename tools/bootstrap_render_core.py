#!/usr/bin/env python3
"""Build and install the exact locked Render Core from a verified source archive."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tarfile


MAX_ARCHIVE_MEMBERS = 50_000
MAX_UNPACKED_BYTES = 2 * 1024 * 1024 * 1024
MAX_MEMBER_BYTES = 1024 * 1024 * 1024


def lock_value(lock_text: str, name: str) -> str:
    match = re.search(rf'set\(\s*{re.escape(name)}\s+"([^"]+)"\s*\)', lock_text)
    if not match:
        raise ValueError(f"Render Core lock is missing {name}")
    return match.group(1)


def read_lock(path: Path) -> dict[str, str]:
    text = path.read_text(encoding="utf-8")
    values = {
        "version": lock_value(text, "JELLYFRAME_RENDER_CORE_LOCKED_VERSION"),
        "abi": lock_value(text, "JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI"),
        "source_hash": lock_value(text, "JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH").lower(),
    }
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?", values["version"]):
        raise ValueError("locked Render Core version is invalid")
    if not re.fullmatch(r"[0-9a-f]{64}", values["source_hash"]):
        raise ValueError("locked Render Core source hash is not a SHA-256 digest")
    if not re.fullmatch(r"[1-9][0-9]*", values["abi"]):
        raise ValueError("locked Render Core ABI is invalid")
    return values


def archive_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _safe_member_path(name: str, root_name: str) -> PurePosixPath:
    if "\\" in name or ":" in name:
        raise ValueError(f"unsafe archive member path: {name!r}")
    path = PurePosixPath(name)
    if path.is_absolute() or not path.parts or path.parts[0] != root_name:
        raise ValueError(f"archive member is outside {root_name}: {name!r}")
    if any(part in ("", ".", "..") for part in path.parts):
        raise ValueError(f"unsafe archive member path: {name!r}")
    return path


def extract_source_archive(archive_path: Path, destination: Path, root_name: str) -> Path:
    """Extract regular files only, rejecting links and paths outside one root."""
    if destination.exists():
        raise ValueError(f"extraction directory already exists: {destination}")
    total_bytes = 0
    with tarfile.open(archive_path, mode="r:gz") as archive:
        members = archive.getmembers()
        if not members or len(members) > MAX_ARCHIVE_MEMBERS:
            raise ValueError("source archive has an invalid member count")
        checked: list[tuple[tarfile.TarInfo, PurePosixPath]] = []
        for member in members:
            relative = _safe_member_path(member.name.rstrip("/"), root_name)
            if not (member.isdir() or member.isfile()):
                raise ValueError(f"source archive contains a link or special file: {member.name!r}")
            if member.isfile():
                if member.size < 0 or member.size > MAX_MEMBER_BYTES:
                    raise ValueError(f"source archive member exceeds its size limit: {member.name!r}")
                total_bytes += member.size
                if total_bytes > MAX_UNPACKED_BYTES:
                    raise ValueError("source archive exceeds the unpacked-size limit")
            checked.append((member, relative))

        destination.mkdir(parents=True, exist_ok=False)
        for member, relative in checked:
            target = destination.joinpath(*relative.parts)
            target.resolve().relative_to(destination.resolve())
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(member)
            if source is None:
                raise ValueError(f"could not read source archive member: {member.name!r}")
            with source, target.open("xb") as output:
                remaining = member.size
                while remaining:
                    block = source.read(min(1024 * 1024, remaining))
                    if not block:
                        raise ValueError(f"truncated source archive member: {member.name!r}")
                    output.write(block)
                    remaining -= len(block)
            try:
                target.chmod(0o644)
            except OSError:
                pass
    return destination / root_name


def installed_identity(prefix: Path) -> dict[str, str | int]:
    configs = list(prefix.glob("**/JellyFrameRenderCoreConfig.cmake"))
    manifests = list(prefix.glob("**/jellyframe_render_core_source_manifest.json"))
    profiles = list(prefix.glob("**/jellyframe_render_core_profile.json"))
    if len(configs) != 1 or len(manifests) != 1 or len(profiles) != 1:
        raise ValueError("installed package is missing unique config, manifest or profile metadata")
    config = configs[0].read_text(encoding="utf-8")

    def config_value(name: str) -> str:
        match = re.search(rf'^set\({re.escape(name)} "([^"]+)"\)$', config, re.MULTILINE)
        if not match:
            raise ValueError(f"installed Core config is missing {name}")
        return match.group(1)

    manifest = json.loads(manifests[0].read_text(encoding="utf-8"))
    profile = json.loads(profiles[0].read_text(encoding="utf-8"))
    result: dict[str, str | int] = {
        "version": config_value("JellyFrameRenderCore_PACKAGE_VERSION"),
        "abi": config_value("JellyFrameRenderCore_ENGINE_ABI"),
        "source_hash": config_value("JellyFrameRenderCore_SOURCE_HASH").lower(),
        "source_file_count": int(config_value("JellyFrameRenderCore_SOURCE_FILE_COUNT")),
    }
    if (manifest.get("sourceHash", "").lower() != result["source_hash"] or
            manifest.get("sourceFileCount") != result["source_file_count"]):
        raise ValueError("installed Core source manifest does not match its exported identity")
    if (profile.get("packageVersion") != result["version"] or
            str(profile.get("engineAbi")) != result["abi"]):
        raise ValueError("installed Core profile does not match its exported package identity")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path, help="verified Render Core .tar.gz")
    parser.add_argument("--expected-sha256", required=True, help="trusted artifact SHA-256")
    parser.add_argument("--workspace", required=True, type=Path,
                        help="new output directory; it must not already exist")
    parser.add_argument("--configuration", default="Release",
                        choices=("Debug", "Release", "RelWithDebInfo", "MinSizeRel"))
    parser.add_argument("--generator", help="optional CMake generator")
    parser.add_argument("--architecture", help="CMake -A value, when supported by the generator")
    parser.add_argument("--parallel", type=int, default=max(1, min(4, os.cpu_count() or 1)))
    parser.add_argument("--cmake", default="cmake", help="CMake executable")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if not re.fullmatch(r"[0-9a-fA-F]{64}", args.expected_sha256):
            raise ValueError("--expected-sha256 must contain exactly 64 hexadecimal characters")
        if args.parallel < 1:
            raise ValueError("--parallel must be a positive integer")
        archive = args.archive.resolve(strict=True)
        if not archive.is_file():
            raise ValueError("--archive must name a regular file")
        actual_archive_hash = archive_sha256(archive)
        if actual_archive_hash.lower() != args.expected_sha256.lower():
            raise ValueError(f"archive SHA-256 mismatch: expected {args.expected_sha256}, got {actual_archive_hash}")

        workspace = args.workspace.resolve()
        if workspace.exists():
            raise ValueError(f"workspace already exists; choose a new path: {workspace}")
        lock = read_lock(Path(__file__).resolve().parents[1] / "cmake/jellyframe_dependency_lock.cmake")
        root_name = f"jellyframe-render-core-{lock['version']}"
        workspace.mkdir(parents=True, exist_ok=False)
        source_parent = workspace / "source"
        source = extract_source_archive(archive, source_parent, root_name)
        build = workspace / "build"
        prefix = workspace / "install"
        configure = [args.cmake, "-S", str(source), "-B", str(build),
                     f"-DCMAKE_BUILD_TYPE={args.configuration}",
                     f"-DCMAKE_INSTALL_PREFIX={prefix}", "-DJELLYFRAME_BUILD_TESTS=OFF",
                     "-DJELLYFRAME_BUILD_BENCHMARKS=OFF", "-DJELLYFRAME_INSTALL_RENDER_CORE=ON"]
        if args.generator:
            configure.extend(("-G", args.generator))
        if args.architecture:
            configure.extend(("-A", args.architecture))
        subprocess.run(configure, check=True)
        subprocess.run([args.cmake, "--build", str(build), "--config", args.configuration,
                        "--parallel", str(args.parallel)], check=True)
        subprocess.run([args.cmake, "--install", str(build), "--config", args.configuration], check=True)

        identity = installed_identity(prefix)
        expected = {"version": lock["version"], "abi": lock["abi"], "source_hash": lock["source_hash"]}
        for key, value in expected.items():
            if identity[key] != value:
                raise ValueError(f"installed Core {key} mismatch: expected {value}, got {identity[key]}")
        result = {
            "archive": str(archive),
            "archive_sha256": actual_archive_hash,
            "package_prefix": str(prefix),
            **identity,
            "configuration": args.configuration,
            "source_hash_verified": True,
        }
        (workspace / "bootstrap-result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2))
        return 0
    except (OSError, ValueError, tarfile.TarError, subprocess.CalledProcessError) as error:
        print(f"Render Core bootstrap failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
