"""Shared version helpers for current JellyFrame development-line tools."""

from __future__ import annotations

import re
import os
from pathlib import Path


RELEASE_VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
RENDER_CORE_LOCK_PATTERN = re.compile(
    r'^set\(JELLYFRAME_RENDER_CORE_LOCKED_VERSION\s+"([0-9]+\.[0-9]+\.[0-9]+)"\)',
    re.MULTILINE,
)


def active_runtime_release_version() -> str:
    version_file = Path(__file__).resolve().parent.parent / "VERSION"
    version = version_file.read_text(encoding="utf-8").strip().split("-", 1)[0]
    if not RELEASE_VERSION_PATTERN.fullmatch(version):
        raise RuntimeError(f"VERSION does not declare a release version: {version!r}")
    return version


def active_render_core_release_version() -> str:
    candidate_version = os.environ.get("JELLYFRAME_TEST_RENDER_CORE_VERSION")
    if candidate_version is not None:
        if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.]+)?", candidate_version):
            raise RuntimeError("JELLYFRAME_TEST_RENDER_CORE_VERSION is invalid")
        return candidate_version
    lock_file = Path(__file__).resolve().parent.parent / "cmake" / "jellyframe_dependency_lock.cmake"
    match = RENDER_CORE_LOCK_PATTERN.search(lock_file.read_text(encoding="utf-8"))
    if match is None:
        raise RuntimeError("Render Core dependency lock has no release version")
    return match.group(1)
