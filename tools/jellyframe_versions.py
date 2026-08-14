"""Shared version helpers for current JellyFrame development-line tools."""

from __future__ import annotations

import re
from pathlib import Path


RELEASE_VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")


def active_runtime_release_version() -> str:
    version_file = Path(__file__).resolve().parent.parent / "VERSION"
    version = version_file.read_text(encoding="utf-8").strip().split("-", 1)[0]
    if not RELEASE_VERSION_PATTERN.fullmatch(version):
        raise RuntimeError(f"VERSION does not declare a release version: {version!r}")
    return version
