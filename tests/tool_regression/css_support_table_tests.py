#!/usr/bin/env python3
import csv
import hashlib
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
GENERATOR = REPO_ROOT / "project_tools" / "generate_css_support_table.py"
IMPORTER = REPO_ROOT / "project_tools" / "import_css_support_crosswork.py"

_generator_spec = importlib.util.spec_from_file_location("generate_css_support_table", GENERATOR)
assert _generator_spec is not None and _generator_spec.loader is not None
generate_css_support_table = importlib.util.module_from_spec(_generator_spec)
_generator_spec.loader.exec_module(generate_css_support_table)


class CssSupportTableTests(unittest.TestCase):
    def test_visibility_contract_matches_parser_subset(self):
        with (REPO_ROOT / "docs" / "csswg_support_table.csv").open(encoding="utf-8-sig", newline="") as source:
            rows = {row["feature_id"]: row for row in csv.DictReader(source)}

        expected_statuses = {
            "csswg:property:visibility:css-display-4:1": "partial",
            "csswg:value:hidden:css-display-4:4": "partial",
            "csswg:value:visible:css-display-4:2": "partial",
            "csswg:value:collapse:css-display-4:1": "unsupported",
        }
        for feature_id, status in expected_statuses.items():
            self.assertIn(feature_id, rows)
            self.assertEqual(rows[feature_id]["status"], status)

        parser_source = (REPO_ROOT / "src" / "render_core" / "css_parser.cpp").read_text(encoding="utf-8")
        self.assertIn('property == "visibility"', parser_source)
        self.assertIn('{"visible", "hidden"}', parser_source)

    def test_text_wrap_contract_matches_parser_subset(self):
        with (REPO_ROOT / "docs" / "csswg_support_table.csv").open(encoding="utf-8-sig", newline="") as source:
            rows = {row["feature_id"]: row for row in csv.DictReader(source)}

        expected_notes = {
            "csswg:property:text-wrap:css-text-4:1": "partial",
            "csswg:value:wrap:css-text-4:4": "partial",
            "csswg:value:nowrap:css-text-4:5": "partial",
        }
        for feature_id, status in expected_notes.items():
            self.assertIn(feature_id, rows)
            self.assertEqual(rows[feature_id]["status"], status)

        parser_source = (REPO_ROOT / "src" / "render_core" / "css_parser.cpp").read_text(encoding="utf-8")
        style_source = (REPO_ROOT / "src" / "render_core" / "style.cpp").read_text(encoding="utf-8")
        self.assertIn('property == "text-wrap"', parser_source)
        self.assertIn('{"wrap", "nowrap"}', parser_source)
        self.assertIn('{"text-wrap", CascadeProperty::WhiteSpace}', style_source)

    def test_package_background_url_contract_matches_parser_subset(self):
        with (REPO_ROOT / "docs" / "csswg_support_table.csv").open(encoding="utf-8-sig", newline="") as source:
            rows = {row["feature_id"]: row for row in csv.DictReader(source)}

        for feature_id in (
            "csswg:value:uri:css2:1",
            "csswg:function:url:css-values-4:1",
        ):
            self.assertIn(feature_id, rows)
            self.assertEqual(rows[feature_id]["status"], "partial")
            self.assertIn("package-absolute", rows[feature_id]["app_author_note"])

        parser_source = (REPO_ROOT / "src" / "render_core" / "css_parser.cpp").read_text(encoding="utf-8")
        style_source = (REPO_ROOT / "src" / "render_core" / "style.cpp").read_text(encoding="utf-8")
        self.assertIn("is_supported_package_background_url", parser_source)
        self.assertIn("parse_package_background_image_url", style_source)
        self.assertIn("pack_background_image_resource", style_source)

    def test_checked_in_tables_match_csv(self):
        completed = subprocess.run([sys.executable, str(GENERATOR), "--check"], cwd=REPO_ROOT, check=False, capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

    def test_source_hash_is_stable_across_checkout_line_endings(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-css-table-") as directory:
            source = Path(directory) / "table.csv"
            source.write_bytes(b"feature_id,kind\r\nfixture,property\r\n")
            expected = hashlib.sha256(b"feature_id,kind\nfixture,property\n").hexdigest()
            self.assertEqual(generate_css_support_table.source_sha256(source), expected)

    def test_importer_normalizes_internal_statuses(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-css-table-") as directory:
            root = Path(directory)
            input_path = root / "crosswork.csv"
            output_path = root / "table.csv"
            fields = ("feature_id", "kind", "name", "context", "spec", "url", "jellyframe_status")
            with input_path.open("w", encoding="utf-8", newline="") as output:
                writer = csv.DictWriter(output, fieldnames=fields)
                writer.writeheader()
                fixtures = (
                    ("feature-0", "supported"),
                    ("detail", "supported_detail"),
                    ("detail", "supported_detail"),
                    ("feature-3", "missing"),
                    ("feature-4", "excluded_legacy_compat"),
                )
                for feature_id, status in fixtures:
                    writer.writerow({"feature_id": feature_id, "kind": "property", "name": "fixture", "context": "", "spec": "css-test", "url": "https://example.test/", "jellyframe_status": status})
            completed = subprocess.run([sys.executable, str(IMPORTER), "--input", str(input_path), "--output", str(output_path)], cwd=REPO_ROOT, check=False, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            with output_path.open(encoding="utf-8", newline="") as source:
                rows = list(csv.DictReader(source))
            self.assertEqual([row["status"] for row in rows], ["supported", "partial", "partial", "unsupported", "out_of_scope"])
            self.assertEqual([row["feature_id"] for row in rows], ["feature-0", "detail", "detail#2", "feature-3", "feature-4"])

    def test_generator_rejects_duplicate_ids(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-css-table-") as directory:
            root = Path(directory)
            csv_path = root / "table.csv"
            fields = ("feature_id", "kind", "area", "name", "context", "status", "app_author_note", "spec_url")
            with csv_path.open("w", encoding="utf-8", newline="") as output:
                writer = csv.DictWriter(output, fieldnames=fields)
                writer.writeheader()
                for status in ("supported", "partial"):
                    writer.writerow({"feature_id": "duplicate", "kind": "property", "area": "css-test", "name": "fixture", "context": "", "status": status, "app_author_note": "test", "spec_url": "https://example.test/"})
            completed = subprocess.run([sys.executable, str(GENERATOR), "--csv", str(csv_path), "--english", str(root / "en.md"), "--chinese", str(root / "zh.md")], cwd=REPO_ROOT, check=False, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 2)
            self.assertIn("duplicate feature_id", completed.stderr)


if __name__ == "__main__":
    unittest.main()
