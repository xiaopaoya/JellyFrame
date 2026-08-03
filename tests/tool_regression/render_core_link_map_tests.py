#!/usr/bin/env python3
"""Regression tests for Render Core profile/link-map agreement."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CHECKER = REPO_ROOT / "tools" / "check_render_core_link_map.py"


def run_checker(
    profile: dict,
    map_text: str,
    *extra_args: str,
) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="jellyframe-link-map-") as directory:
        root = Path(directory)
        profile_path = root / "profile.json"
        map_path = root / "engine.map"
        profile_path.write_text(json.dumps(profile), encoding="utf-8")
        map_path.write_text(map_text, encoding="utf-8")
        return subprocess.run(
            [
                sys.executable, str(CHECKER),
                "--profile", str(profile_path), "--map", str(map_path),
                *extra_args,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )


def run_scoped_checker(
    profile: dict,
    map_text: str,
    *used_features: str,
) -> subprocess.CompletedProcess[str]:
    extra_args = []
    for feature in used_features:
        extra_args.extend(["--used-feature", feature])
    return run_checker(
        profile,
        map_text,
        "--scope-used-features",
        *extra_args,
    )


class RenderCoreLinkMapTests(unittest.TestCase):
    def test_enabled_family_markers_are_required(self):
        result = run_checker(
            {"schemaVersion": 1, "features": [
                "core.document", "core.paint", "css.modern-paint", "graphics.canvas2d",
            ]},
            "jellyframe_render_core:modern_paint_fill_linear_gradient\n"
            "jellyframe_render_core:canvas2d.cpp.obj\n",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_disabled_family_uses_stub_without_real_marker(self):
        result = run_checker(
            {"schemaVersion": 1, "features": []},
            "jellyframe_render_core:canvas2d_disabled.cpp.obj\n",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_advanced_forms_marker_agrees_with_profile(self):
        enabled = run_checker(
            {"schemaVersion": 1, "features": [
                "core.document", "core.paint", "forms.advanced",
            ]},
            "jellyframe_render_core:form_submission.cpp.obj\n",
        )
        self.assertEqual(enabled.returncode, 0, enabled.stdout + enabled.stderr)

        disabled = run_checker(
            {"schemaVersion": 1, "features": ["core.document", "core.paint"]},
            "jellyframe_render_core:form_submission_disabled.cpp.obj\n",
        )
        self.assertEqual(disabled.returncode, 0, disabled.stdout + disabled.stderr)

    def test_msvc_style_object_markers_are_accepted(self):
        result = run_checker(
            {"schemaVersion": 1, "features": [
                "core.document", "core.paint", "forms.advanced", "graphics.canvas2d",
            ]},
            "jellyframe_render_core.lib(form_submission.obj)\n"
            "jellyframe_render_core.lib(canvas2d.obj)\n",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_profile_map_mismatch_fails(self):
        result = run_checker(
            {"schemaVersion": 1, "features": ["core.document", "core.paint", "css.modern-paint"]},
            "jellyframe_render_core:software_renderer.cpp.obj\n",
        )
        self.assertNotEqual(result.returncode, 0)

    def test_empty_scoped_workload_allows_enabled_families_to_be_dead_stripped(self):
        result = run_scoped_checker(
            {
                "schemaVersion": 1,
                "features": ["core.document", "core.paint", "graphics.canvas2d"],
            },
            "jellyframe_render_core:software_renderer.cpp.obj\n",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('"usedFeatures": []', result.stdout)
        self.assertEqual(result.stdout.count('"status": "not-tested"'), 1)
        self.assertIn("css.modern-paint", result.stdout)

    def test_incomplete_profile_dependency_closure_fails_before_map_check(self):
        result = run_checker(
            {"schemaVersion": 1, "features": ["graphics.canvas2d"]},
            "jellyframe_render_core:canvas2d.cpp.obj\n",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("graphics.canvas2d -> core.paint", result.stderr)

    def test_unused_enabled_family_can_be_dead_stripped_when_workload_is_scoped(self):
        profile = {
            "schemaVersion": 1,
            "features": ["core.document", "core.paint", "css.modern-paint", "graphics.canvas2d"],
        }
        map_text = "jellyframe_render_core:modern_paint_fill_linear_gradient\n"
        result = run_checker(
            profile,
            map_text,
            "--used-feature", "css.modern-paint",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('"status": "not-tested"', result.stdout)

        result = run_checker(
            profile,
            map_text,
        )
        self.assertNotEqual(result.returncode, 0)

    def test_scoped_canvas_workload_still_requires_canvas_symbol(self):
        result = run_checker(
            {
                "schemaVersion": 1,
                "features": ["core.document", "core.paint", "graphics.canvas2d"],
            },
            "jellyframe_render_core:software_renderer.cpp.obj\n",
            "--used-feature", "graphics.canvas2d",
        )
        self.assertNotEqual(result.returncode, 0)

    def test_flex_grid_is_explicitly_not_applicable_to_map_symbols(self):
        result = run_checker(
            {
                "schemaVersion": 1,
                "features": ["core.document", "core.paint", "css.flex-grid"],
            },
            "jellyframe_render_core:layout.cpp.obj\n",
            "--used-feature", "css.flex-grid",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('"feature": "css.flex-grid"', result.stdout)
        self.assertIn('"mapValidation": "not-applicable"', result.stdout)
        self.assertIn('"status": "not-applicable"', result.stdout)


if __name__ == "__main__":
    unittest.main()
