#!/usr/bin/env python3
import csv
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
GENERATOR = REPO_ROOT / "tools" / "generate_html_support_table.py"


class HtmlSupportTableTests(unittest.TestCase):
    def test_checked_in_tables_match_csv(self):
        completed = subprocess.run(
            [sys.executable, str(GENERATOR), "--check"],
            cwd=REPO_ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

    def test_generator_rejects_duplicate_ids(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-html-table-") as directory:
            root = Path(directory)
            csv_path = root / "table.csv"
            with csv_path.open("w", encoding="utf-8", newline="") as output:
                writer = csv.DictWriter(output, fieldnames=(
                    "feature_id", "type", "area", "name", "parent", "spec_section",
                    "status", "app_author_note", "spec_url",
                ))
                writer.writeheader()
                for status in ("supported", "partial"):
                    writer.writerow({
                        "feature_id": "duplicate", "type": "html-element", "area": "forms",
                        "name": "input", "parent": "", "spec_section": "4.10",
                        "status": status, "app_author_note": "test", "spec_url": "https://example.test/",
                    })
            completed = subprocess.run(
                [sys.executable, str(GENERATOR), "--csv", str(csv_path),
                 "--english", str(root / "en.md"), "--chinese", str(root / "zh.md")],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 2)
            self.assertIn("duplicate feature_id", completed.stderr)

    def test_generator_recovers_legacy_unquoted_note_commas(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-html-table-") as directory:
            root = Path(directory)
            csv_path = root / "table.csv"
            csv_path.write_text(
                "feature_id,type,area,name,parent,spec_section,status,app_author_note,spec_url\n"
                "fixture,html-element,forms,input,,4.10,partial,First clause, second clause,https://example.test/\n",
                encoding="utf-8",
            )
            english_path = root / "en.md"
            completed = subprocess.run(
                [sys.executable, str(GENERATOR), "--csv", str(csv_path),
                 "--english", str(english_path), "--chinese", str(root / "zh.md")],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            generated = english_path.read_text(encoding="utf-8")
            self.assertIn("First clause, second clause", generated)
            self.assertIn("[link](https://example.test/)", generated)


if __name__ == "__main__":
    unittest.main()
