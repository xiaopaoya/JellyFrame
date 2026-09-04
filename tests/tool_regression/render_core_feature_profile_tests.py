#!/usr/bin/env python3
"""Regression tests for generated Render Core feature profiles."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


BASE_FEATURES = ("core.document", "core.paint")
LOCKED_SOURCE_HASH = "539a894519d3251f02c8b3aee8d0d0fb715bf49a732fc74126ccb2188462e3f0"
TEST_ARGS: argparse.Namespace | None = None

PROFILE_CASES = (
    (True, True, True, True, "render-core-default"),
    (False, True, True, True, "render-core-no-canvas"),
    (True, False, True, True, "render-core-no-modern-paint"),
    (True, True, False, True, "render-core-no-flex-grid"),
    (False, False, True, True, "render-core-no-modern-paint-no-canvas"),
    (False, True, False, True, "render-core-no-flex-grid-no-canvas"),
    (True, False, False, True, "render-core-no-flex-grid-no-modern-paint"),
    (False, False, False, True, "render-core-minimal"),
    (True, True, True, False, "render-core-no-forms-advanced"),
    (False, True, True, False, "render-core-no-canvas-no-forms-advanced"),
    (True, False, True, False, "render-core-no-modern-paint-no-forms-advanced"),
    (True, True, False, False, "render-core-no-flex-grid-no-forms-advanced"),
    (False, False, True, False, "render-core-no-modern-paint-no-canvas-no-forms-advanced"),
    (False, True, False, False, "render-core-no-flex-grid-no-canvas-no-forms-advanced"),
    (True, False, False, False, "render-core-no-flex-grid-no-modern-paint-no-forms-advanced"),
    (False, False, False, False, "render-core-minimal-no-forms-advanced"),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--generator")
    return parser.parse_args()


class RenderCoreFeatureProfileTests(unittest.TestCase):
    cmake: Path
    generator: str | None
    source_root: Path

    @classmethod
    def setUpClass(cls) -> None:
        if TEST_ARGS is None:
            raise RuntimeError("test arguments were not initialized")
        cls.cmake = TEST_ARGS.cmake
        cls.generator = TEST_ARGS.generator
        cls.source_root = TEST_ARGS.source_root

    def configure_profile(
        self,
        output_dir: Path,
        *,
        canvas: bool,
        modern_paint: bool,
        flex_grid: bool,
        advanced_forms: bool,
    ) -> dict:
        def cmake_bool(value: bool) -> str:
            return "ON" if value else "OFF"

        command = [
            str(self.cmake),
            "-S", str(self.source_root),
            "-B", str(output_dir),
            "-DJELLYFRAME_BUILD_EXAMPLES=OFF",
            "-DJELLYFRAME_BUILD_TESTS=OFF",
            "-DJELLYFRAME_BUILD_BENCHMARKS=OFF",
            f"-DJELLYFRAME_ENABLE_CANVAS2D={cmake_bool(canvas)}",
            f"-DJELLYFRAME_ENABLE_MODERN_PAINT={cmake_bool(modern_paint)}",
            f"-DJELLYFRAME_ENABLE_FLEX_GRID={cmake_bool(flex_grid)}",
            f"-DJELLYFRAME_ENABLE_ADVANCED_FORMS={cmake_bool(advanced_forms)}",
        ]
        if self.generator:
            command.extend(["-G", self.generator])
        result = subprocess.run(command, text=True, capture_output=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        generated_dir = output_dir / "generated"
        profile_path = generated_dir / "jellyframe_render_core_profile.json"
        provenance_path = generated_dir / "jellyframe_render_core_provenance.json"
        source_manifest_path = generated_dir / "jellyframe_render_core_source_manifest.json"
        return (
            json.loads(profile_path.read_text(encoding="utf-8")),
            json.loads(provenance_path.read_text(encoding="utf-8")),
            json.loads(source_manifest_path.read_text(encoding="utf-8")),
        )

    def test_all_optional_family_combinations_have_consistent_profiles(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jellyframe-profile-") as directory:
            root = Path(directory)
            build_dir = root / "build"
            expected_source_hash: str | None = None
            for canvas, modern_paint, flex_grid, advanced_forms, profile_id in PROFILE_CASES:
                with self.subTest(profile_id=profile_id):
                    profile, provenance, source_manifest = self.configure_profile(
                        build_dir,
                        canvas=canvas,
                        modern_paint=modern_paint,
                        flex_grid=flex_grid,
                        advanced_forms=advanced_forms,
                    )
                    expected_features = list(BASE_FEATURES)
                    if advanced_forms:
                        expected_features.append("forms.advanced")
                    if flex_grid:
                        expected_features.append("css.flex-grid")
                    if modern_paint:
                        expected_features.append("css.modern-paint")
                    if canvas:
                        expected_features.append("graphics.canvas2d")

                    self.assertEqual(profile["schemaVersion"], 1)
                    self.assertEqual(profile["packageVersion"], "0.6.1")
                    self.assertEqual(profile["profileId"], profile_id)
                    self.assertEqual(profile["features"], expected_features)
                    self.assertEqual(provenance["schemaVersion"], 1)
                    self.assertEqual(provenance["consumer"], "jellyframe-runtime")
                    self.assertEqual(provenance["provider"], "in-tree")
                    self.assertEqual(provenance["packageVersion"], "0.6.1")
                    self.assertEqual(provenance["engineAbi"], 1)
                    self.assertEqual(provenance["profileFile"], "jellyframe_render_core_profile.json")
                    self.assertEqual(provenance["lockedVersion"], "0.6.2")
                    self.assertEqual(provenance["lockedEngineAbi"], 1)
                    self.assertEqual(provenance["lockedSourceHash"], LOCKED_SOURCE_HASH)
                    self.assertFalse(provenance["lockEnforced"])
                    self.assertEqual(provenance["sourceManifestFile"],
                                     "jellyframe_render_core_source_manifest.json")
                    self.assertEqual(source_manifest["schemaVersion"], 1)
                    self.assertEqual(source_manifest["hashAlgorithm"], "sha256")
                    self.assertGreater(source_manifest["sourceFileCount"], 0)
                    self.assertRegex(source_manifest["sourceHash"], r"^[0-9a-f]{64}$")
                    self.assertEqual(len(source_manifest["files"]), source_manifest["sourceFileCount"])
                    self.assertEqual([entry["path"] for entry in source_manifest["files"]],
                                     sorted(entry["path"] for entry in source_manifest["files"]))
                    for entry in source_manifest["files"]:
                        self.assertRegex(entry["path"], r"^(cmake|src/render_core)/")
                        self.assertRegex(entry["sha256"], r"^[0-9a-f]{64}$")
                    self.assertEqual(provenance["sourceHash"], source_manifest["sourceHash"])
                    self.assertEqual(provenance["sourceFileCount"], source_manifest["sourceFileCount"])
                    if expected_source_hash is None:
                        expected_source_hash = source_manifest["sourceHash"]
                    self.assertEqual(source_manifest["sourceHash"], expected_source_hash)
                    source_families = profile["sourceFamilies"]
                    self.assertEqual(
                        set(source_families["core.document"])
                        | set(source_families["core.paint"]),
                        {
                            "src/render_core/animation_invalidation.cpp",
                            "src/render_core/animation_timeline.cpp",
                            "src/render_core/arena.cpp",
                            "src/render_core/bitmap_font.cpp",
                            "src/render_core/bitmap_font_resource.cpp",
                            "src/render_core/css_parser.cpp",
                            "src/render_core/display_invalidation.cpp",
                            "src/render_core/dirty_region.cpp",
                            "src/render_core/document_script.cpp",
                            "src/render_core/document_style.cpp",
                            "src/render_core/dom.cpp",
                            "src/render_core/dom_owner.cpp",
                            "src/render_core/embedded_framebuffer.cpp",
                            "src/render_core/event.cpp",
                            "src/render_core/form_control.cpp",
                            "src/render_core/frame_loop.cpp",
                            "src/render_core/frame_update.cpp",
                            "src/render_core/hit_test.cpp",
                            "src/render_core/html_parser.cpp",
                            "src/render_core/html_tokenizer.cpp",
                            "src/render_core/html_tree_builder.cpp",
                            "src/render_core/input.cpp",
                            "src/render_core/layer_tree.cpp",
                            "src/render_core/layout.cpp",
                            "src/render_core/pipeline_statistics.cpp",
                            "src/render_core/render_tree.cpp",
                            "src/render_core/scroll_blit.cpp",
                            "src/render_core/software_renderer.cpp",
                            "src/render_core/style.cpp",
                            "src/render_core/style_repaint.cpp",
                            "src/render_core/text_adapter.cpp",
                            "src/render_core/text_backend.cpp",
                            "src/render_core/text_layout_reuse.cpp",
                            "src/render_core/text_normalization.cpp",
                            "src/render_core/text_repaint.cpp",
                            "src/render_core/text_scan.cpp",
                        },
                    )
                    self.assertFalse(
                        set(source_families["core.document"])
                        & set(source_families["core.paint"]),
                        "mandatory source families must not overlap",
                    )
                    self.assertEqual(
                        source_families["forms.advanced"],
                        (["src/render_core/form_submission.cpp"] if advanced_forms else
                         ["src/render_core/form_submission_disabled.cpp"]),
                    )
                    self.assertEqual(
                        source_families["css.flex-grid"],
                        (["src/render_core/flex_grid_paint.cpp"] if flex_grid else []),
                    )
                    self.assertEqual(
                        profile["notes"],
                        {
                            "canvas2d": "1" if canvas else "0",
                            "modernPaint": "1" if modern_paint else "0",
                            "flexGrid": "1" if flex_grid else "0",
                            "formsAdvanced": "1" if advanced_forms else "0",
                        },
                    )


if __name__ == "__main__":
    TEST_ARGS = parse_args()
    unittest.main(argv=[sys.argv[0]])
