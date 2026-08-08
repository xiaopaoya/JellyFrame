#!/usr/bin/env python3
"""Check the single declarative Render Core feature registry."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from render_core_feature_registry import (  # noqa: E402
    KNOWN_RENDER_CORE_FEATURES,
    RENDER_CORE_FEATURE_DEPENDENCIES,
    RENDER_CORE_FEATURE_METADATA,
)


class RenderCoreFeatureRegistryTests(unittest.TestCase):
    def test_catalog_contains_expected_families_and_metadata(self) -> None:
        self.assertEqual(
            KNOWN_RENDER_CORE_FEATURES,
            {
                "core.document", "core.paint", "forms.advanced", "css.flex-grid",
                "css.modern-paint", "graphics.canvas2d",
            },
        )
        self.assertEqual(RENDER_CORE_FEATURE_DEPENDENCIES["core.paint"], {"core.document"})
        self.assertEqual(RENDER_CORE_FEATURE_DEPENDENCIES["css.modern-paint"], {"core.paint"})
        self.assertEqual(
            RENDER_CORE_FEATURE_METADATA["graphics.canvas2d"],
            {"option": "JELLYFRAME_ENABLE_CANVAS2D", "suffix": "no-canvas", "order": "30"},
        )

    def test_every_dependency_is_registered(self) -> None:
        for dependencies in RENDER_CORE_FEATURE_DEPENDENCIES.values():
            self.assertTrue(dependencies <= KNOWN_RENDER_CORE_FEATURES)


if __name__ == "__main__":
    unittest.main()
