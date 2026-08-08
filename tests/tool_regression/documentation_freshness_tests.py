#!/usr/bin/env python3
"""Keep versioned first-party Markdown discoverably fresh.

The date is intentionally author-maintained: a test cannot infer whether prose
still describes the current implementation. It can, however, prevent a new or
forgotten document from silently omitting the repository's visible freshness
and version contract.
"""

import re
import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CURRENT_VERSION = (REPO_ROOT / "VERSION").read_text(encoding="utf-8").strip()
EXCLUDED_PREFIXES = ("third_party/",)


def accepted_document_versions(version: str) -> tuple[str, ...]:
    """Return the current version and, for a dev cycle, its released base."""
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)(-.+)?", version)
    if match is None:
        raise RuntimeError(f"VERSION is not a semantic version: {version!r}")
    major, minor, patch = (int(match.group(index)) for index in range(1, 4))
    if match.group(4) is None:
        return (version,)
    if patch > 0:
        previous_release = f"{major}.{minor}.{patch - 1}"
    elif minor > 0:
        previous_release = f"{major}.{minor - 1}.0"
    else:
        previous_release = None
    return (version,) if previous_release is None else (version, previous_release)


class DocumentationFreshnessTests(unittest.TestCase):
    def test_first_party_markdown_has_current_freshness_metadata(self):
        completed = subprocess.run(
            ["git", "ls-files", "*.md"],
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        version_pattern = "|".join(
            re.escape(version) for version in accepted_document_versions(CURRENT_VERSION))
        metadata_patterns = (
            re.compile(rf"Last updated: \d{{4}}-\d{{2}}-\d{{2}}; Applies to: (?:{version_pattern})(?=$|[\s.,;:。；，])"),
            re.compile(rf"最后更新：\d{{4}}-\d{{2}}-\d{{2}}；适用版本：(?:{version_pattern})(?=$|[\s.,;:。；，])"),
            # Generated support tables expose the source audit date instead of
            # a hand-maintained edit timestamp, but carry the same version.
            re.compile(rf"Source audit: \d{{4}}-\d{{2}}-\d{{2}}; Applies to: (?:{version_pattern})(?=$|[\s.,;:。；，])"),
            re.compile(rf"审计快照：\d{{4}}-\d{{2}}-\d{{2}}；适用版本：(?:{version_pattern})(?=$|[\s.,;:。；，])"),
        )
        missing = []
        for relative in completed.stdout.splitlines():
            if relative.startswith(EXCLUDED_PREFIXES):
                continue
            content = (REPO_ROOT / relative).read_text(encoding="utf-8")
            if not any(pattern.search(content) for pattern in metadata_patterns):
                missing.append(relative)

        self.assertEqual(
            missing,
            [],
            "first-party Markdown must declare a current or immediately previous release version: "
            + ", ".join(missing),
        )


if __name__ == "__main__":
    unittest.main()
