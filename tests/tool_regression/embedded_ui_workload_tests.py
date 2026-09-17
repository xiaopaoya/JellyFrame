#!/usr/bin/env python3
import json
import unittest
from html.parser import HTMLParser
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PORT_ROOT = REPO_ROOT / "ports" / "esp32s3-idf"
RESOURCE_ROOT = PORT_ROOT / "resources" / "app"
WORKLOADS = {
    "static": "CONFIG_JELLYFRAME_ESP32S3_EMBEDDED_UI_WORKLOAD_STATIC=y",
    "local_update": "CONFIG_JELLYFRAME_ESP32S3_EMBEDDED_UI_WORKLOAD_LOCAL_UPDATE=y",
    "scroll": "CONFIG_JELLYFRAME_ESP32S3_EMBEDDED_UI_WORKLOAD_SCROLL=y",
    "full_repaint": "CONFIG_JELLYFRAME_ESP32S3_EMBEDDED_UI_WORKLOAD_FULL_REPAINT=y",
}


def reject_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


class WorkloadHtmlParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.stylesheets = set()

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if attributes.get("id"):
            self.ids.add(attributes["id"])
        if tag == "link" and attributes.get("rel") == "stylesheet":
            self.stylesheets.add(attributes.get("href", ""))


class EmbeddedUiWorkloadTests(unittest.TestCase):
    def test_metadata_is_unique_and_references_existing_resources(self):
        metadata_path = RESOURCE_ROOT / "embedded_ui_workload.metadata.json"
        metadata = json.loads(
            metadata_path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicate_keys,
        )
        self.assertEqual(metadata["format"], "jellyframe.ws147.embedded-ui-workload.metadata.v1")
        self.assertEqual(metadata["viewport"], "172x320")
        self.assertEqual(metadata["pixelFormat"], "RGB565")
        self.assertTrue(metadata["domNodeCountInvariant"])
        self.assertEqual(metadata["initialState"]["quietHours"], "22:00")
        for resource in metadata["resources"]:
            self.assertTrue((RESOURCE_ROOT / resource.lstrip("/")).is_file(), resource)

    def test_document_exposes_every_native_workload_target(self):
        parser = WorkloadHtmlParser()
        parser.feed((RESOURCE_ROOT / "embedded_ui_workload.html").read_text(encoding="utf-8"))
        self.assertEqual(parser.stylesheets, {"styles/embedded_ui_workload.css"})
        self.assertTrue({
            "screen",
            "status-card",
            "status-text",
            "status-level",
            "settings-panel",
            "setting-toggle",
            "setting-toggle-2",
        }.issubset(parser.ids))
        css = (RESOURCE_ROOT / "styles" / "embedded_ui_workload.css").read_text(encoding="utf-8")
        self.assertIn('#screen[data-screen="alt"]', css)

    def test_workload_profiles_only_differ_by_selected_action(self):
        common_profiles = None
        for name, selected in WORKLOADS.items():
            path = PORT_ROOT / f"sdkconfig.ws147_embedded_ui_{name}.defaults"
            lines = {
                line.strip()
                for line in path.read_text(encoding="utf-8").splitlines()
                if line.strip()
            }
            self.assertIn("CONFIG_JELLYFRAME_ESP32S3_RUN_EMBEDDED_UI_WORKLOAD=y", lines)
            self.assertIn(selected, lines)
            self.assertIn("CONFIG_JELLYFRAME_ESP32S3_DEVICE_PERFORMANCE_PROFILE=y", lines)
            self.assertIn("CONFIG_JELLYFRAME_ESP32S3_DEVICE_PERFORMANCE_WARMUP_FRAMES=30", lines)
            self.assertIn("CONFIG_JELLYFRAME_ESP32S3_DEVICE_PERFORMANCE_WINDOW_FRAMES=120", lines)
            self.assertIn("CONFIG_JELLYFRAME_ESP32S3_DEVICE_PERFORMANCE_START_DELAY_MS=15000", lines)
            shared = lines - set(WORKLOADS.values())
            if common_profiles is None:
                common_profiles = shared
            else:
                self.assertEqual(shared, common_profiles, path.name)

    def test_native_full_repaint_mutates_the_styled_screen_node(self):
        source = (PORT_ROOT / "main" / "jellyframe_esp32s3_ui_task.cpp").read_text(encoding="utf-8")
        self.assertIn('find_by_id(*context->document, "screen")', source)
        self.assertIn('screen->set_attribute("data-screen", active ? "alt" : "base")', source)
        self.assertNotIn('context->document->set_attribute("data-screen", active ? "alt" : "base")', source)


if __name__ == "__main__":
    unittest.main()
