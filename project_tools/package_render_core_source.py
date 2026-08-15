#!/usr/bin/env python3
"""Create a deterministic standalone JellyFrame Render Core source archive."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import re
import tarfile
from pathlib import Path


ARCHIVE_PREFIX = "jellyframe-render-core"
VERSION_PATTERN = re.compile(
    r'set\(JELLYFRAME_RENDER_CORE_PACKAGE_VERSION\s+"([^"]+)"', re.MULTILINE
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="JellyFrame source checkout containing Render Core",
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def require_file(path: Path) -> Path:
    if not path.is_file():
        raise RuntimeError(f"required Render Core source file is missing: {path}")
    return path


def package_version(source_root: Path) -> str:
    version_file = require_file(source_root / "cmake" / "render_core_version.cmake")
    match = VERSION_PATTERN.search(version_file.read_text(encoding="utf-8"))
    if match is None:
        raise RuntimeError(f"could not read Render Core package version from: {version_file}")
    return match.group(1)


def source_files(source_root: Path) -> list[Path]:
    files = {
        require_file(source_root / "LICENSE"),
        require_file(source_root / "cmake" / "JellyFrameRenderCoreConfig.cmake.in"),
        require_file(source_root / "cmake" / "render_core_feature_registry.csv"),
    }
    cmake_dir = source_root / "cmake"
    for pattern in ("render_core_*.cmake", "render_core_*.json.in"):
        files.update(path for path in cmake_dir.glob(pattern) if path.is_file())
    render_core_dir = source_root / "src" / "render_core"
    if not render_core_dir.is_dir():
        raise RuntimeError(f"Render Core source directory is missing: {render_core_dir}")
    files.update(path for path in render_core_dir.rglob("*") if path.is_file())
    return sorted(files, key=lambda path: path.relative_to(source_root).as_posix())


def standalone_entry_file(source_root: Path, monorepo_path: Path, standalone_path: Path) -> Path:
    if monorepo_path.is_file():
        return monorepo_path
    return require_file(standalone_path)


def archive_member(root_name: str, source_root: Path, path: Path) -> str:
    return f"{root_name}/{path.relative_to(source_root).as_posix()}"


def add_file(archive: tarfile.TarFile, name: str, content: bytes) -> None:
    info = tarfile.TarInfo(name)
    info.size = len(content)
    info.mode = 0o644
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    info.mtime = 0
    archive.addfile(info, fileobj=io.BytesIO(content))


def create_archive(source_root: Path, output_dir: Path) -> tuple[Path, Path]:
    source_root = source_root.resolve()
    output_dir = output_dir.resolve()
    render_core_dir = source_root / "src" / "render_core"
    if output_dir.is_relative_to(render_core_dir):
        raise RuntimeError(
            "--output-dir must not be inside src/render_core; generated archives "
            "must not become source-archive inputs"
        )
    version = package_version(source_root)
    root_name = f"{ARCHIVE_PREFIX}-{version}"
    output_dir.mkdir(parents=True, exist_ok=True)
    archive_path = output_dir / f"{root_name}.tar.gz"
    checksum_path = archive_path.with_suffix(archive_path.suffix + ".sha256")
    root_cmake = standalone_entry_file(
        source_root,
        source_root / "cmake" / "render_core_standalone_root.cmake",
        source_root / "CMakeLists.txt",
    )
    presets = standalone_entry_file(
        source_root,
        source_root / "cmake" / "render_core_standalone_presets.json.in",
        source_root / "CMakePresets.json",
    )
    standalone_readme = standalone_entry_file(
        source_root,
        source_root / "src" / "render_core" / "STANDALONE_README.md",
        source_root / "README.md",
    )

    archive_entries = {
        f"{root_name}/CMakeLists.txt": root_cmake.read_bytes(),
        f"{root_name}/CMakePresets.json": presets.read_bytes(),
        f"{root_name}/README.md": standalone_readme.read_bytes(),
    }
    for path in source_files(source_root):
        archive_entries[archive_member(root_name, source_root, path)] = path.read_bytes()

    with archive_path.open("wb") as raw_file:
        with gzip.GzipFile(fileobj=raw_file, mode="wb", filename="", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.GNU_FORMAT) as archive:
                for name in sorted(archive_entries):
                    add_file(archive, name, archive_entries[name])

    digest = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    checksum_path.write_text(
        f"{digest}  {archive_path.name}\n", encoding="ascii", newline="\n"
    )
    return archive_path, checksum_path


def main() -> int:
    args = parse_args()
    archive_path, checksum_path = create_archive(args.source_root, args.output_dir)
    print(archive_path)
    print(checksum_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
