#!/usr/bin/env python3
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import jellyframe_cli  # noqa: E402
import package_app  # noqa: E402


def tiny_bmp(width: int = 2, height: int = 2, bits_per_pixel: int = 24) -> bytes:
    bytes_per_pixel = bits_per_pixel // 8
    row_stride = ((width * bits_per_pixel + 31) // 32) * 4
    pixel_bytes = row_stride * height
    file_size = 54 + pixel_bytes
    header = bytearray()
    header.extend(b"BM")
    header.extend(struct.pack("<IHHI", file_size, 0, 0, 54))
    header.extend(struct.pack("<IiiHHIIiiII",
                              40,
                              width,
                              height,
                              1,
                              bits_per_pixel,
                              0,
                              pixel_bytes,
                              2835,
                              2835,
                              0,
                              0))
    pixels = bytearray(pixel_bytes)
    for y in range(height):
        for x in range(width):
            offset = y * row_stride + x * bytes_per_pixel
            pixels[offset:offset + 3] = bytes([x * 80, y * 80, 180])
            if bytes_per_pixel == 4:
                pixels[offset + 3] = 255
    return bytes(header + pixels)


def tiny_png_header(width: int = 2, height: int = 2) -> bytes:
    return (
        b"\x89PNG\r\n\x1a\n"
        + struct.pack(">I", 13)
        + b"IHDR"
        + struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
        + b"\0\0\0\0"
    )


class PackagePreflightTests(unittest.TestCase):
    def test_font_preflight_scans_text_resources_and_skips_binary_other(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-tool-regression-") as directory:
            root = Path(directory)
            for relative in (
                "index.html",
                "styles/app.css",
                "scripts/app.js",
                "capture_system_events.jfcapture",
                "audio/tone.wav",
                "assets/icon.bmp",
            ):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"sample")

            report = {
                "resources": [
                    {"kind": "Other", "path": "/index.html"},
                    {"kind": "Stylesheet", "path": "/styles/app.css"},
                    {"kind": "ClassicScript", "path": "/scripts/app.js"},
                    {"kind": "Other", "path": "/capture_system_events.jfcapture"},
                    {"kind": "Other", "path": "/audio/tone.wav"},
                    {"kind": "Image", "path": "/assets/icon.bmp"},
                ]
            }
            report_path = root / "report.json"
            report_path.write_text(json.dumps(report), encoding="utf-8")

            files = [Path(path).relative_to(root).as_posix()
                     for path in jellyframe_cli.resource_files_from_report(root, report_path)]

        self.assertEqual(
            files,
            [
                "index.html",
                "styles/app.css",
                "scripts/app.js",
                "capture_system_events.jfcapture",
            ],
        )

    def test_audio_files_remain_generic_package_resources(self):
        self.assertEqual(
            package_app.resource_kind(Path("audio/tone.wav")),
            "jellyframe::HostResourceKind::Other",
        )

    def test_image_diagnostics_classify_codec_and_target_support(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-image-diagnostics-") as directory:
            root = Path(directory)
            bmp = root / "icon.bmp"
            png = root / "photo.png"
            gif = root / "anim.gif"
            bmp.write_bytes(tiny_bmp())
            png.write_bytes(tiny_png_header())
            gif.write_bytes(b"GIF89a")
            resources = [
                package_app.build_resource_entry(root, bmp, "/icon.bmp", 0),
                package_app.build_resource_entry(root, png, "/photo.png", 0),
                package_app.build_resource_entry(root, gif, "/anim.gif", 0),
            ]

            diagnostics, warnings = package_app.collect_image_diagnostics(resources, {
                "id": "bmp-only",
                "hostServices": {
                    "imageDecode": True,
                    "imageCodecs": ["bmp"],
                },
            })

        entries = {entry["path"]: entry for entry in diagnostics["entries"]}
        self.assertEqual(diagnostics["codecCounts"], {"bmp": 1, "gif": 1, "png": 1})
        self.assertEqual(entries["/icon.bmp"]["codec"], "bmp")
        self.assertEqual(entries["/icon.bmp"]["targetSupport"], "supported")
        self.assertEqual(entries["/icon.bmp"]["metadata"]["width"], 2)
        self.assertEqual(entries["/photo.png"]["codec"], "png")
        self.assertEqual(entries["/photo.png"]["targetSupport"], "unsupported")
        self.assertEqual(entries["/photo.png"]["metadata"]["height"], 2)
        codes = [warning["code"] for warning in warnings]
        self.assertIn("image-codec-target-unsupported", codes)
        self.assertIn("image-codec-unsupported", codes)

    def test_invalid_bmp_reports_specific_warning(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-invalid-bmp-") as directory:
            root = Path(directory)
            bmp = root / "broken.bmp"
            bmp.write_bytes(b"BM")
            resources = [package_app.build_resource_entry(root, bmp, "/broken.bmp", 0)]

            diagnostics, warnings = package_app.collect_image_diagnostics(resources, {
                "id": "bmp-target",
                "hostServices": {"imageDecode": True, "imageCodecs": ["bmp"]},
            })

        self.assertEqual(diagnostics["entries"][0]["metadata"]["reason"], "invalid-signature")
        self.assertEqual(warnings[0]["code"], "image-bmp-invalid")

    def test_mp3_capability_warns_when_packaged_audio_is_not_mp3(self):
        manifest = {"capabilities": ["media.audio.mp3"]}
        resources = [{"path": "/audio/tone.wav"}]

        warnings = package_app.collect_audio_resource_warnings(manifest, resources)

        self.assertEqual(len(warnings), 1)
        self.assertEqual(warnings[0]["code"], "audio-capability-resource-mismatch")

    def test_mp3_capability_accepts_packaged_mp3_resource(self):
        manifest = {"capabilities": ["media.audio.mp3"]}
        resources = [{"path": "/audio/tone.mp3"}]

        self.assertEqual(package_app.collect_audio_resource_warnings(manifest, resources), [])

    def test_runtime_budget_estimate_reports_package_known_usage(self):
        resources = [
            {"size": 100},
            {"size": 50},
        ]
        budgets = {
            "maxResourceBytes": 128,
            "maxDomNodes": 512,
            "maxTimers": 4,
            "maxEventListeners": 16,
        }
        font_diagnostics = {
            "runtimeFontBudget": {
                "maxAppFonts": 1,
                "maxAppFontBytes": 64,
                "maxAppFontGlyphs": 8,
            },
            "usableRuntimeFontCount": 1,
            "runtimeFontBytes": 32,
            "runtimeFontGlyphs": 4,
        }

        estimate = package_app.collect_runtime_budget_estimate(resources, budgets, font_diagnostics)

        self.assertEqual(estimate["format"], "jellyframe.runtime-budget.estimate")
        self.assertEqual(estimate["resources"], {"used": 150, "limit": 128, "exhausted": True})
        self.assertEqual(estimate["domNodes"]["limit"], 512)
        self.assertEqual(estimate["timers"]["limit"], 4)
        self.assertEqual(estimate["eventListeners"]["limit"], 16)
        self.assertEqual(estimate["appFonts"], {"used": 1, "limit": 1, "exhausted": True})
        self.assertEqual(estimate["appFontBytes"], {"used": 32, "limit": 64, "exhausted": False})

    def test_service_intent_report_summarizes_manifest_capabilities(self):
        manifest = package_app.validate_manifest({
            "format": "jellyframe.app",
            "formatVersion": 0,
            "id": "org.example.services",
            "version": {"name": "1.0.0", "code": 1},
            "entry": "/index.html",
            "runtime": {"minJellyFrame": "0.4.0", "script": "classic"},
            "viewport": {"designWidth": 300, "designHeight": 300},
            "budgets": {"maxResourceBytes": 4096},
            "permissions": ["network"],
            "capabilities": [
                "network.fetch",
                "storage.kv",
                "media.audio.mp3",
                "sensor.accelerometer",
                "location.position",
            ],
            "backgroundServices": {
                "network": {"whileSuspended": True, "whileScreenOff": False},
                "audio": {"whileSuspended": True, "whileScreenOff": True},
                "sensors": {"whileSuspended": False, "whileScreenOff": False, "inLowPower": False},
                "location": {"whileSuspended": True, "whileScreenOff": False, "inLowPower": False},
            },
            "targets": {
                "round-300": {
                    "viewport": {"width": 300, "height": 300},
                    "output": "jfapp",
                }
            },
        })

        intent = package_app.service_intent_report(manifest, {"id": "round-300"})

        self.assertEqual(intent["target"], "round-300")
        self.assertEqual(
            intent["requested"],
            {
                "networkFetch": True,
                "storageKv": True,
                "audioPlayback": True,
                "sensorAccelerometer": True,
                "sensorGyroscope": False,
                "sensorHeartRate": False,
                "sensorAmbientLight": False,
                "locationPosition": True,
            },
        )
        self.assertEqual(
            intent["targetSupport"],
            {
                "networkFetch": "unknown",
                "storageKv": "unknown",
                "audioPlayback": "unknown",
                "sensorAccelerometer": "unknown",
                "sensorGyroscope": "unknown",
                "sensorHeartRate": "unknown",
                "sensorAmbientLight": "unknown",
                "locationPosition": "unknown",
            },
        )
        self.assertTrue(intent["backgroundServices"]["audio"]["whileScreenOff"])
        self.assertTrue(intent["backgroundServices"]["location"]["whileSuspended"])
        self.assertTrue(any("remote HTML" in note for note in intent["policyNotes"]))

        supported_intent = package_app.service_intent_report(manifest, {
            "id": "round-300",
            "hostServices": {
                "networkFetch": True,
                "storageKv": True,
                "audioPlayback": False,
                "sensorAccelerometer": True,
                "locationPosition": False,
            },
        })
        self.assertEqual(
            supported_intent["targetSupport"],
            {
                "networkFetch": "supported",
                "storageKv": "supported",
                "audioPlayback": "unsupported",
                "sensorAccelerometer": "supported",
                "sensorGyroscope": "unknown",
                "sensorHeartRate": "unknown",
                "sensorAmbientLight": "unknown",
                "locationPosition": "unsupported",
            },
        )

        warnings = package_app.collect_service_target_warnings(manifest, {
            "id": "round-300",
            "hostServices": {
                "networkFetch": True,
                "storageKv": False,
                "audioPlayback": False,
                "sensorAccelerometer": False,
                "locationPosition": False,
            },
        })
        self.assertEqual(
            [warning["service"] for warning in warnings],
            ["storageKv", "audioPlayback", "sensorAccelerometer", "locationPosition"],
        )
        self.assertTrue(all(warning["code"] == "service-target-unsupported" for warning in warnings))

    def test_responsive_profile_status_and_report_merge(self):
        pipeline_report = {
            "viewport": {"width": 320, "height": 240},
            "layout": {
                "contentHeight": 320,
                "horizontalOverflow": False,
                "verticalOverflow": True,
                "bounds": {"left": 0, "top": 0, "right": 300, "bottom": 320},
            },
            "pipeline": {
                "domNodes": 12,
                "renderObjects": 10,
                "layoutBoxes": 10,
                "layers": 2,
                "displayCommands": 8,
                "framebufferBytes": 307200,
                "estimatedHeapBytes": 333000,
            },
            "frameUpdate": {
                "action": "rebuild-pipeline",
                "repaint": "full-frame",
                "reason": "first-paint",
            },
            "summary": {"total": 0, "info": 0, "warning": 0, "error": 0},
        }

        profile = jellyframe_cli.responsive_profile_from_pipeline(
            "rect-320x240",
            {"viewport": {"shape": "rect"}},
            pipeline_report)

        self.assertEqual(profile["status"], "scroll-needed")
        self.assertEqual(profile["viewport"], {"width": 320, "height": 240, "shape": "rect"})
        self.assertEqual(profile["layout"]["contentHeight"], 320)
        self.assertEqual(profile["frameUpdate"]["reason"], "first-paint")

        pipeline_report["layout"]["horizontalOverflow"] = True
        self.assertEqual(jellyframe_cli.responsive_status(pipeline_report), "horizontal-overflow")

        with tempfile.TemporaryDirectory(prefix="jellyframe-responsive-report-") as directory:
            report_path = Path(directory) / "report.json"
            report_path.write_text(json.dumps({"format": "jellyframe.package.report"}), encoding="utf-8")
            jellyframe_cli.merge_responsive_profiles(report_path, [profile])
            merged = json.loads(report_path.read_text(encoding="utf-8"))

        self.assertEqual(merged["responsiveProfiles"][0]["target"], "rect-320x240")

    def test_requested_targets_are_explicit_opt_in(self):
        class Args:
            target = "round-300"
            targets = ""
            all_targets = False

        self.assertEqual(jellyframe_cli.requested_targets(Args()), [])
        Args.targets = "round-300, rect-320x240,round-300"
        self.assertEqual(jellyframe_cli.requested_targets(Args()), ["round-300", "rect-320x240"])

    def test_font_family_usage_matches_manifest_fonts(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-font-family-") as directory:
            root = Path(directory)
            css = root / "styles" / "app.css"
            css.parent.mkdir(parents=True, exist_ok=True)
            css.write_text(
                '.title { font-family: "Jelly Tiny", system-ui, sans-serif; }\n'
                '.body { font-family: MissingFace, serif; }\n',
                encoding="utf-8")
            resources = [{
                "path": "/styles/app.css",
                "kind": "jellyframe::HostResourceKind::Stylesheet",
                "file": css,
                "size": css.stat().st_size,
            }]

            usage = package_app.collect_font_family_usage(resources, [{
                "id": "tiny",
                "source": "/fonts/tiny.jffont",
                "family": "Jelly Tiny",
            }])

        statuses = {entry["family"]: entry["status"] for entry in usage["entries"]}
        self.assertEqual(statuses["Jelly Tiny"], "manifest-runtime-font")
        self.assertEqual(statuses["system-ui"], "generic")
        self.assertEqual(statuses["MissingFace"], "unmatched-primary")
        self.assertEqual(usage["unmatchedPrimaryCount"], 1)

    def test_font_axis_metadata_is_diagnostic_only(self):
        warnings = package_app.collect_manifest_warnings({
            "format": "jellyframe.app",
            "formatVersion": 0,
            "id": "org.example.fonts",
            "version": {"name": "1.0.0", "code": 1},
            "entry": "/index.html",
            "runtime": {"minJellyFrame": "0.4.0", "script": "none"},
            "viewport": {"designWidth": 300, "designHeight": 300},
            "budgets": {"maxResourceBytes": 4096},
            "fonts": [
                {
                    "id": "missing-axis",
                    "source": "/fonts/missing.jffont",
                    "profile": "tiny",
                    "license": {"name": "Example", "source": "example.bdf"},
                },
                {
                    "id": "bad-axis",
                    "source": "/fonts/bad.jffont",
                    "profile": "tiny",
                    "license": {"name": "Example", "source": "example.bdf"},
                    "sizes": [0, 12],
                    "weights": [400, 1200],
                },
            ],
            "targets": {"round-300": {"viewport": {"width": 300, "height": 300}, "output": "jfapp"}},
        })
        codes = [warning["code"] for warning in warnings]
        self.assertEqual(codes.count("font-axis-metadata-missing"), 2)
        self.assertEqual(codes.count("font-axis-metadata-invalid"), 2)

        self.assertEqual(package_app.normalized_int_list([8, True, "12", 16], 1), [8, 16])
        self.assertEqual(package_app.normalized_int_list([400, 1200], 1, 1000), [400])


if __name__ == "__main__":
    unittest.main()
