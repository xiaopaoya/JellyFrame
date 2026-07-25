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
VERSION = (REPO_ROOT / "VERSION").read_text(encoding="utf-8").strip()
EXCLUDED_PREFIXES = ("third_party/",)


class DocumentationFreshnessTests(unittest.TestCase):
    def test_first_party_markdown_has_current_freshness_metadata(self):
        completed = subprocess.run(
            ["git", "ls-files", "*.md"],
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        expected_version = re.escape(VERSION)
        metadata_patterns = (
            re.compile(rf"Last updated: \d{{4}}-\d{{2}}-\d{{2}}; Applies to: {expected_version}"),
            re.compile(rf"最后更新：\d{{4}}-\d{{2}}-\d{{2}}；适用版本：{expected_version}"),
            # Generated support tables expose the source audit date instead of
            # a hand-maintained edit timestamp, but carry the same version.
            re.compile(rf"Source audit: \d{{4}}-\d{{2}}-\d{{2}}; Applies to: {expected_version}"),
            re.compile(rf"审计快照：\d{{4}}-\d{{2}}-\d{{2}}；适用版本：{expected_version}"),
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
            "first-party Markdown must declare a current date and applies-to version: "
            + ", ".join(missing),
        )


if __name__ == "__main__":
    unittest.main()
