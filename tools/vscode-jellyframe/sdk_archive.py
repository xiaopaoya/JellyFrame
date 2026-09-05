#!/usr/bin/env python3
"""Safely extract a downloaded JellyFrame App Author SDK archive."""

import argparse
import json
import os
import stat
import sys
import zipfile
from pathlib import Path

MAX_MEMBERS = 20000
MAX_UNCOMPRESSED_BYTES = 512 * 1024 * 1024


def safe_member_path(root: Path, name: str) -> Path:
    root = root.resolve()
    if not name or "\x00" in name:
        raise ValueError("archive contains an invalid member name")
    candidate = Path(name)
    if candidate.is_absolute() or any(part in ("", ".", "..") for part in candidate.parts):
        raise ValueError(f"archive contains an unsafe member path: {name}")
    destination = (root / candidate).resolve()
    if root not in destination.parents:
        raise ValueError(f"archive member escapes destination: {name}")
    return destination


def extract(archive_path: Path, destination: Path) -> str:
    if destination.exists():
        if not destination.is_dir() or any(destination.iterdir()):
            raise ValueError("extraction destination must be an empty directory")
    else:
        destination.mkdir(parents=True)
    with zipfile.ZipFile(archive_path) as archive:
        members = archive.infolist()
        if len(members) > MAX_MEMBERS:
            raise ValueError("archive contains too many files")
        if sum(info.file_size for info in members) > MAX_UNCOMPRESSED_BYTES:
            raise ValueError("archive exceeds the uncompressed size limit")
        roots = set()
        for info in members:
            target = safe_member_path(destination, info.filename)
            roots.add(Path(info.filename).parts[0])
            mode = (info.external_attr >> 16) & 0xFFFF
            if stat.S_ISLNK(mode):
                raise ValueError(f"archive contains a symbolic link: {info.filename}")
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(info) as source, target.open("xb") as output:
                while chunk := source.read(1024 * 1024):
                    output.write(chunk)
    if len(roots) != 1:
        raise ValueError("SDK archive must contain exactly one top-level directory")
    root_name = next(iter(roots))
    if not (destination / root_name / "tools" / "jellyframe_cli.py").is_file():
        raise ValueError("archive is not a JellyFrame App Author SDK")
    return root_name


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps({"root": extract(args.archive.resolve(), args.destination.resolve())}))
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"SDK archive rejected: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
