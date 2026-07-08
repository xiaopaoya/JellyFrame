#!/usr/bin/env python3
import json
import re
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
    def test_esp32s3_render_core_sources_match_desktop_target(self):
        desktop_cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        esp_cmake = (REPO_ROOT / "ports" / "esp32s3-idf" / "components" /
                     "jellyframe_render_core" / "CMakeLists.txt").read_text(encoding="utf-8")
        pattern = r"src/render_core/([A-Za-z0-9_]+\.cpp)"
        desktop_sources = set(re.findall(pattern, desktop_cmake))
        esp_sources = set(re.findall(pattern, esp_cmake))

        self.assertEqual(sorted(desktop_sources - esp_sources), [])
        self.assertEqual(sorted(esp_sources - desktop_sources), [])

    def test_weather_template_and_sample_stay_intentionally_aligned(self):
        mirrored_files = (
            "index.html",
            "styles/app.css",
            "scripts/app.js",
            "assets/cloudy.bmp",
            "assets/haze.bmp",
            "assets/rain.bmp",
            "assets/sunny.bmp",
        )
        sample_root = REPO_ROOT / "samples" / "apps" / "packages" / "watch_weather"
        template_root = REPO_ROOT / "tools" / "templates" / "apps" / "weather"
        for relative in mirrored_files:
            self.assertEqual(
                (sample_root / relative).read_bytes(),
                (template_root / relative).read_bytes(),
                f"weather template drifted from sample: {relative}",
            )

        sample_manifest = json.loads((sample_root / "jellyframe.app.json").read_text(encoding="utf-8"))
        template_manifest = json.loads((template_root / "jellyframe.app.json").read_text(encoding="utf-8"))
        shared_fields = (
            "format",
            "formatVersion",
            "version",
            "entry",
            "runtime",
            "viewport",
            "budgets",
            "permissions",
            "capabilities",
        )
        for field in shared_fields:
            self.assertEqual(
                sample_manifest.get(field),
                template_manifest.get(field),
                f"weather template manifest drifted from sample field: {field}",
            )

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

    def test_package_resource_paths_reject_colon_like_runtime(self):
        self.assertEqual(package_app.normalize_app_path("assets/icon.bmp"), "/assets/icon.bmp")
        for path in ("foo:bar", "app://assets/icon.bmp", "https://example.test/app.css"):
            with self.assertRaises(SystemExit):
                package_app.normalize_app_path(path)

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

    def test_packaged_audio_warns_without_playback_capability(self):
        manifest = {"capabilities": []}
        resources = [{"path": "/audio/tone.wav"}]

        warnings = package_app.collect_audio_resource_warnings(manifest, resources)

        self.assertEqual(len(warnings), 1)
        self.assertEqual(warnings[0]["code"], "audio-capability-resource-mismatch")

    def test_playback_capability_accepts_packaged_audio_resource(self):
        manifest = {"capabilities": ["media.audio.playback"]}
        resources = [{"path": "/audio/tone.wav"}]

        self.assertEqual(package_app.collect_audio_resource_warnings(manifest, resources), [])

    def test_script_api_diagnostics_warn_when_manifest_capability_is_missing(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-script-api-diagnostics-") as directory:
            root = Path(directory)
            script = root / "scripts" / "app.js"
            inline = root / "index.html"
            script.parent.mkdir(parents=True, exist_ok=True)
            script.write_text(
                "var xhr = new XMLHttpRequest();\n"
                "localStorage.setItem('k', 'v');\n"
                "new Audio('/audio/tone.wav').play();\n",
                encoding="utf-8")
            inline.write_text(
                "<script>navigator.geolocation.getCurrentPosition(function(){});"
                "canvas.getContext('2d');</script>",
                encoding="utf-8")
            resources = [
                package_app.build_resource_entry(root, script, "/scripts/app.js", 0),
                package_app.build_resource_entry(root, inline, "/index.html", 0),
            ]

            diagnostics, warnings = package_app.collect_script_api_diagnostics({
                "capabilities": ["network.fetch", "graphics.canvas2d"],
            }, resources)

        self.assertEqual(diagnostics["entryCount"], 5)
        self.assertEqual(
            sorted((warning["api"], warning["capability"]) for warning in warnings),
            [
                ("Audio", "media.audio.playback"),
                ("localStorage", "storage.kv"),
                ("navigator.geolocation", "location.position"),
            ],
        )
        self.assertTrue(all(warning["code"] == "script-capability-missing" for warning in warnings))

    def test_script_api_diagnostics_warn_for_ambient_date_construction(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-script-date-diagnostics-") as directory:
            root = Path(directory)
            script = root / "scripts" / "app.js"
            script.parent.mkdir(parents=True, exist_ok=True)
            script.write_text(
                "var unsafe = new Date();\n"
                "var alsoUnsafe = Date();\n"
                "var safe = new Date(Date.now());\n",
                encoding="utf-8")
            resources = [package_app.build_resource_entry(root, script, "/scripts/app.js", 0)]

            diagnostics, warnings = package_app.collect_script_api_diagnostics({}, resources)

        self.assertEqual(diagnostics["entryCount"], 1)
        self.assertEqual(len(warnings), 1)
        self.assertEqual(warnings[0]["code"], "script-host-time-ambiguous")
        self.assertEqual(warnings[0]["api"], "Date")

    def test_script_api_diagnostics_warn_for_deferred_web_apis(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-script-deferred-api-diagnostics-") as directory:
            root = Path(directory)
            script = root / "scripts" / "app.js"
            script.parent.mkdir(parents=True, exist_ok=True)
            script.write_text(
                "fetch('/data.json');\n"
                "Promise.resolve(1);\n"
                "view.innerHTML = '<b>unsafe</b>';\n"
                "view.getBoundingClientRect();\n"
                "view.setPointerCapture(1);\n"
                "import('./chunk.js');\n",
                encoding="utf-8")
            resources = [package_app.build_resource_entry(root, script, "/scripts/app.js", 0)]

            diagnostics, warnings = package_app.collect_script_api_diagnostics({}, resources)

        self.assertEqual(diagnostics["entryCount"], 6)
        self.assertEqual(
            sorted(warning["api"] for warning in warnings),
            ["Promise", "dynamic import", "fetch", "getBoundingClientRect", "innerHTML", "pointer capture"],
        )
        self.assertTrue(all(warning["code"] == "script-api-deferred" for warning in warnings))

    def test_script_api_diagnostics_accept_simple_query_selector_subset(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-script-query-selector-diagnostics-") as directory:
            root = Path(directory)
            script = root / "scripts" / "app.js"
            script.parent.mkdir(parents=True, exist_ok=True)
            script.write_text(
                "document.querySelector('.card');\n"
                "document.querySelectorAll('button.primary');\n"
                "panel.querySelector('[data-op=\"+\"]');\n",
                encoding="utf-8")
            resources = [package_app.build_resource_entry(root, script, "/scripts/app.js", 0)]

            diagnostics, warnings = package_app.collect_script_api_diagnostics({}, resources)

        self.assertEqual(diagnostics["entryCount"], 0)
        self.assertEqual(warnings, [])

    def test_script_api_diagnostics_warn_for_complex_query_selector_subset(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-script-query-selector-subset-") as directory:
            root = Path(directory)
            script = root / "scripts" / "app.js"
            script.parent.mkdir(parents=True, exist_ok=True)
            script.write_text(
                "document.querySelector('main > button');\n"
                "document.querySelectorAll(selectorFromState);\n",
                encoding="utf-8")
            resources = [package_app.build_resource_entry(root, script, "/scripts/app.js", 0)]

            diagnostics, warnings = package_app.collect_script_api_diagnostics({}, resources)

        self.assertEqual(diagnostics["entryCount"], 1)
        self.assertEqual(len(warnings), 1)
        self.assertEqual(warnings[0]["code"], "script-api-subset")
        self.assertEqual(warnings[0]["api"], "querySelector")

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

    def test_total_resource_budget_warning_reports_packaged_sum(self):
        warnings = package_app.collect_resource_budget_warnings(
            [{"size": 100}, {"size": 50}],
            {"maxResourceBytes": 128})

        self.assertEqual(len(warnings), 1)
        self.assertEqual(warnings[0]["code"], "resource-budget-exceeded")
        self.assertEqual(warnings[0]["used"], 150)
        self.assertEqual(warnings[0]["limit"], 128)

    def test_resource_discovery_rejects_symlinks_when_supported(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-symlink-resource-") as directory:
            root = Path(directory)
            (root / "index.html").write_text("<body>ok</body>", encoding="utf-8")
            target = root / "outside.txt"
            target.write_text("secret", encoding="utf-8")
            link = root / "assets" / "linked.txt"
            link.parent.mkdir()
            try:
                link.symlink_to(target)
            except OSError as exc:
                self.skipTest(f"symlink creation unavailable: {exc}")

            with self.assertRaises(SystemExit):
                package_app.discover_resources(root, 0)

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
                "graphics.canvas2d",
                "media.audio.playback",
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
                "canvas2d": True,
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
                "canvas2d": "unknown",
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
                "canvas2d": False,
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
                "canvas2d": "unsupported",
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
                "canvas2d": False,
                "audioPlayback": False,
                "sensorAccelerometer": False,
                "locationPosition": False,
            },
        })
        self.assertEqual(
            [warning["service"] for warning in warnings],
            ["storageKv", "canvas2d", "audioPlayback", "sensorAccelerometer", "locationPosition"],
        )
        self.assertTrue(all(warning["code"] == "service-target-unsupported" for warning in warnings))

    def test_authorized_file_capabilities_are_known_but_not_storage(self):
        manifest = {
            "format": "jellyframe.app",
            "formatVersion": 0,
            "id": "org.example.file.manager",
            "version": {"name": "1.0.0", "code": 1},
            "entry": "/index.html",
            "runtime": {"minJellyFrame": "0.5.0", "script": "classic"},
            "viewport": {"designWidth": 300, "designHeight": 300},
            "budgets": {"maxResourceBytes": 4096},
            "capabilities": ["file.read", "file.write", "file.manage"],
            "targets": {"round-300": {"viewport": {"width": 300, "height": 300}, "output": "jfapp"}},
        }

        warnings = package_app.collect_manifest_warnings(manifest)
        self.assertNotIn("manifest-capability-unknown", [warning["code"] for warning in warnings])
        normalized = package_app.validate_manifest(manifest)
        intent = package_app.service_intent_report(normalized, {"id": "round-300"})
        self.assertFalse(intent["requested"]["storageKv"])

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

    def test_responsive_gate_decision_is_reported_and_counted(self):
        profile = {
            "status": "horizontal-overflow",
            "viewport": {"width": 172, "height": 320, "shape": "rect"},
            "layout": {"horizontalOverflow": True},
            "diagnostics": {"warning": 2, "error": 0},
        }
        gate = jellyframe_cli.responsive_gate_for_profile(profile, {
            "policy": "reject",
            "allowHorizontalOverflow": False,
            "maxWarnings": 1,
            "minViewport": {"width": 200, "height": 300},
        })

        self.assertEqual(gate["decision"], "reject")
        self.assertIn("horizontal-overflow", gate["reasons"])
        self.assertIn("warnings>1", gate["reasons"])
        self.assertIn("viewport-width<200", gate["reasons"])

        with tempfile.TemporaryDirectory(prefix="jellyframe-responsive-gate-") as directory:
            report_path = Path(directory) / "report.json"
            profile["gate"] = gate
            report_path.write_text(json.dumps({
                "format": "jellyframe.package.report",
                "responsiveProfiles": [profile],
            }), encoding="utf-8")
            errors, warnings, infos = jellyframe_cli.diagnostic_status_from_report(report_path)

        self.assertEqual(errors, 1)
        self.assertEqual(warnings, 2)
        self.assertEqual(infos, 0)

    def test_doctor_summary_compacts_responsive_gate_results(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-doctor-summary-") as directory:
            report_path = Path(directory) / "sample.report.json"
            report_path.write_text(json.dumps({
                "format": "jellyframe.package.report",
                "warnings": [{"code": "package-warning"}],
                "pipelineDiagnostics": {
                    "summary": {"error": 0, "warning": 1, "info": 2},
                },
                "responsiveProfiles": [{
                    "target": "round-300",
                    "status": "fits",
                    "diagnostics": {"error": 0, "warning": 0, "info": 1},
                    "gate": {"decision": "accept"},
                }, {
                    "target": "rect-172x320",
                    "status": "diagnostics-warning",
                    "diagnostics": {"error": 0, "warning": 2, "info": 0},
                    "gate": {"decision": "warn"},
                }],
            }), encoding="utf-8")

            summary = jellyframe_cli.doctor_summary_from_report("sample", "ok", report_path)
            formatted = jellyframe_cli.format_doctor_summary(summary)

        self.assertEqual(summary["warnings"], 5)
        self.assertEqual(summary["infos"], 3)
        self.assertEqual(summary["targets"][0]["gate"], "accept")
        self.assertIn("sample: ok diagnostics=0/5/3", formatted)
        self.assertIn("perf=unknown/0", formatted)
        self.assertIn("round-300:fits/accept", formatted)
        self.assertIn("rect-172x320:diagnostics-warning/warn", formatted)

    def test_developer_advice_is_derived_from_report_diagnostics(self):
        report = {
            "warnings": [{
                "level": "warning",
                "code": "font-family-unmatched",
                "message": "CSS primary font-family is not declared",
                "source": "/styles/app.css",
            }, {
                "level": "warning",
                "code": "service-target-unsupported",
                "message": "target does not support requested service",
                "source": "jellyframe.app.json",
            }, {
                "level": "warning",
                "code": "script-capability-missing",
                "message": "script uses localStorage but manifest does not declare storage.kv",
                "source": "/scripts/app.js",
            }, {
                "level": "warning",
                "code": "script-host-time-ambiguous",
                "message": "script uses Date() without host time",
                "source": "/scripts/app.js",
            }, {
                "level": "warning",
                "code": "script-api-deferred",
                "message": "script uses fetch",
                "source": "/scripts/app.js",
            }, {
                "level": "warning",
                "code": "script-api-subset",
                "message": "script uses complex querySelector",
                "source": "/scripts/app.js",
            }, {
                "level": "warning",
                "code": "future-diagnostic-code",
                "message": "future warning shape",
                "source": "future",
            }],
            "pipelineDiagnostics": {
                "diagnostics": [{
                    "stage": "layout",
                    "severity": "warning",
                    "code": "layout-text-overflow",
                    "detail": "text=\"Start\" measuredWidth=54 availableWidth=32 contentWidth=32 fontSize=16 node=\"button.primary\" path=\"body:nth-of-type(1)>main:nth-of-type(1)>button.primary:nth-of-type(1)\"",
                }, {
                    "stage": "layout",
                    "severity": "info",
                    "code": "visual-scroll-container",
                    "detail": "node=\"section#hours\" path=\"body:nth-of-type(1)>main:nth-of-type(1)>section#hours\" boxHeight=80 contentHeight=144 overflowY=64",
                }],
            },
            "responsiveProfiles": [{
                "target": "rect-172x320",
                "status": "horizontal-overflow",
                "layout": {"horizontalOverflow": True},
                "diagnostics": {"warning": 1, "error": 0},
                "diagnosticSamples": [{
                    "stage": "layout",
                    "severity": "warning",
                    "code": "layout-text-overflow",
                    "detail": "text=\"Daily\" measuredWidth=45 availableWidth=30 contentWidth=30 fontSize=16 node=\"button.tab\" path=\"body:nth-of-type(1)>main:nth-of-type(1)>button.tab:nth-of-type(2)\"",
                }, {
                    "stage": "layout",
                    "severity": "warning",
                    "code": "visual-horizontal-overflow",
                    "detail": "paintBounds=\"0,0..412,300\" viewport=172x320 overflowRight=240 node=\"div.wide\" path=\"body:nth-of-type(1)>main:nth-of-type(1)>div.wide:nth-of-type(1)\" boxLeft=0 boxRight=412 boxWidth=412 boxOverflowRight=240",
                }],
                "gate": {"decision": "warn", "reasons": ["horizontal-overflow"]},
            }],
            "fontDiagnostics": {
                "missingNonAsciiCodepointCount": 1,
                "fontFamilyUsage": {"unmatchedPrimaryCount": 1},
            },
        }

        advice = jellyframe_cli.collect_developer_advice(report)
        codes = {entry["code"] for entry in advice}

        self.assertIn("font-family-unmatched", codes)
        self.assertIn("service-target-unsupported", codes)
        self.assertIn("script-capability-missing", codes)
        self.assertIn("script-host-time-ambiguous", codes)
        self.assertIn("script-api-deferred", codes)
        self.assertIn("script-api-subset", codes)
        self.assertIn("future-diagnostic-code", codes)
        self.assertIn("layout-text-overflow", codes)
        self.assertIn("visual-scroll-container", codes)
        self.assertIn("visual-horizontal-overflow", codes)
        self.assertIn("target-gate-not-accepted", codes)
        self.assertIn("font-missing-glyphs", codes)
        self.assertTrue(all(entry.get("action") for entry in advice))
        self.assertTrue(any(entry["code"] == "future-diagnostic-code" and
                            "review" in entry["title"].lower()
                            for entry in advice))
        self.assertTrue(any(entry["code"] == "layout-text-overflow" and
                            entry.get("target") == "rect-172x320" and
                            entry.get("text") == "Daily" and
                            entry.get("path") == "body:nth-of-type(1)>main:nth-of-type(1)>button.tab:nth-of-type(2)" and
                            entry.get("metrics", {}).get("availableWidth") == 30 and
                            entry.get("recipe") == "app_author_recipes.md#narrow-targets"
                            for entry in advice))
        self.assertTrue(any(entry["code"] == "visual-scroll-container" and
                            entry.get("path") == "body:nth-of-type(1)>main:nth-of-type(1)>section#hours" and
                            entry.get("metrics", {}).get("overflowY") == 64 and
                            entry.get("recipe") == "app_author_recipes.md#scroll-list"
                            for entry in advice))
        self.assertTrue(any(entry["code"] == "visual-horizontal-overflow" and
                            entry.get("target") == "rect-172x320" and
                            entry.get("path") == "body:nth-of-type(1)>main:nth-of-type(1)>div.wide:nth-of-type(1)" and
                            entry.get("metrics", {}).get("boxOverflowRight") == 240 and
                            entry.get("recipe") == "app_author_recipes.md#narrow-targets"
                            for entry in advice))

    def test_responsive_profile_carries_diagnostic_samples(self):
        profile = jellyframe_cli.responsive_profile_from_pipeline("rect-172x320", {
            "viewport": {"width": 172, "height": 320, "shape": "rect"},
        }, {
            "viewport": {"width": 172, "height": 320},
            "summary": {"warning": 1, "info": 1},
            "diagnostics": [{
                "stage": "css",
                "severity": "info",
                "code": "css-media-query-not-matched",
                "detail": "@media (max-height: 260px)",
            }, {
                "stage": "layout",
                "severity": "warning",
                "code": "layout-text-overflow",
                "detail": "text=\"Hourly\" measuredWidth=60 availableWidth=34 contentWidth=34 fontSize=16 node=\"button.tab\" path=\"body:nth-of-type(1)>main:nth-of-type(1)>button.tab:nth-of-type(1)\"",
            }],
        })

        self.assertEqual(profile["diagnosticSamples"][0]["code"], "layout-text-overflow")
        self.assertEqual(profile["diagnosticSamples"][0]["text"], "Hourly")
        self.assertEqual(profile["diagnosticSamples"][0]["node"], "button.tab")
        self.assertEqual(profile["diagnosticSamples"][0]["path"], "body:nth-of-type(1)>main:nth-of-type(1)>button.tab:nth-of-type(1)")
        self.assertEqual(profile["diagnosticSamples"][0]["metrics"]["measuredWidth"], 60)

    def test_write_json_report_adds_developer_advice(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-advice-report-") as directory:
            report_path = Path(directory) / "report.json"
            jellyframe_cli.write_json_report(report_path, {
                "format": "jellyframe.package.report",
                "pipelineDiagnostics": {
                    "diagnostics": [{
                        "stage": "layout",
                        "severity": "warning",
                        "code": "visual-scroll-needed",
                        "detail": "contentHeight=340 viewportHeight=300",
                    }],
                },
            })
            report = json.loads(report_path.read_text(encoding="utf-8"))

        self.assertEqual(report["developerAdvice"][0]["code"], "visual-scroll-needed")
        self.assertIn("action", report["developerAdvice"][0])

    def test_write_json_report_adds_performance_summary_and_advice(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-performance-report-") as directory:
            report_path = Path(directory) / "report.json"
            jellyframe_cli.write_json_report(report_path, {
                "format": "jellyframe.package.report",
                "runtimeBudgetEstimate": {
                    "resources": {"used": 90, "limit": 100},
                    "displayCommands": {"used": 0, "limit": 120},
                },
                "responsiveProfiles": [{
                    "target": "round-300",
                    "pipeline": {
                        "domNodes": 260,
                        "layers": 20,
                        "displayCommands": 128,
                        "framebufferBytes": 360000,
                        "estimatedHeapBytes": 700000,
                    },
                    "viewport": {"width": 300, "height": 300},
                    "layout": {"horizontalOverflow": False},
                    "frameUpdate": {
                        "action": "rebuild-pipeline",
                        "repaint": "full-frame",
                        "reason": "first-paint",
                    },
                    "timingsUs": {
                        "paint": 9000,
                        "present": 2000,
                        "total": 15000,
                    },
                }],
            })
            report = json.loads(report_path.read_text(encoding="utf-8"))

        self.assertEqual(report["performanceSummary"]["rating"], "high-risk")
        self.assertEqual(report["performanceSummary"]["maxDisplayCommands"], 128)
        self.assertEqual(report["performanceSummary"]["maxTotalPipelineUs"], 15000)
        self.assertEqual(report["performanceSummary"]["slowestMeasuredStage"], {"stage": "paint", "us": 9000})
        self.assertEqual(report["performanceSummary"]["resourceBudgetPercent"], 90)
        codes = {entry["code"] for entry in report["performanceAdvice"]}
        self.assertIn("performance-pipeline-heap-estimate", codes)
        self.assertIn("performance-display-command-count", codes)
        self.assertIn("performance-layer-count", codes)
        self.assertIn("performance-resource-budget-high", codes)

    def test_runtime_capture_log_merges_measured_performance_summary(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-runtime-capture-") as directory:
            root = Path(directory)
            report_path = root / "report.json"
            runtime_log = root / "frame.log"
            runtime_log.write_text(
                "\n".join([
                    "diagnostics: 1",
                    "JellyFrame Win32 browser frame capture",
                    "  output_dir=out/motion_lab_frames",
                    "  montage=out/motion_lab_montage.bmp",
                    "  frames=30",
                    "  frame_step_ms=33",
                    "  viewport=300x300",
                    "  scripting=off",
                    "  dirty_local=30 full=1 clean=0",
                    "  frame_update idle=0 repaint=30 rebuild=1 first_paint=1 paint_dirty=30 text_stable=0 style_stable=0 layout_previous=0 tree_dirty=0 framebuffer_mismatch=0",
                    "  frame_repaint dirty_rect=30 full_frame=1 full_first_paint=1 full_tree_dirty=0 full_framebuffer_mismatch=0 full_resolved_mismatch=0 full_paint_dirty=0 dirty_paint_dirty=30 dirty_text_stable=0 dirty_style_stable=0 dirty_layout_previous=0",
                    "  scroll_blits full=31 fast=0 copied_pixels=2790000",
                    "  present_estimate_rgb565 frames=31 full=1 dirty=30 source_rects=91 clipped_rects=91 empty_rects=0 flushes=91 converted_pixels=1247508 packed_bytes=2495016",
                    "  load_telemetry samples=31 sleep=0 lowfreq=0 normal=19 boost=12 overloaded=1 drop_animation=1 callback_backlog=0 service_backlog=0 max_dirty=97%",
                ]) + "\n",
                encoding="utf-8",
            )
            jellyframe_cli.merge_runtime_capture_report(report_path, runtime_log)
            report = json.loads(report_path.read_text(encoding="utf-8"))

        self.assertEqual(report["runtimeMetrics"]["summary"]["frames"], 31)
        self.assertEqual(report["performanceSummary"]["source"], "package-preflight-estimate+runtime-capture")
        self.assertEqual(report["performanceSummary"]["measuredFrameCount"], 31)
        self.assertEqual(report["performanceSummary"]["measuredFullFrameCount"], 1)
        self.assertEqual(report["performanceSummary"]["measuredConvertedPixels"], 1247508)
        self.assertEqual(report["performanceSummary"]["measuredLoadOverloadedFrames"], 1)
        codes = {entry["code"] for entry in report["performanceAdvice"]}
        self.assertIn("performance-runtime-overloaded", codes)
        self.assertIn("performance-runtime-drop-animation", codes)
        self.assertIn("performance-runtime-dirty-area-high", codes)

    def test_port_telemetry_log_merges_real_device_performance_summary(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-port-telemetry-") as directory:
            root = Path(directory)
            report_path = root / "report.json"
            telemetry_log = root / "port.log"
            telemetry_log.write_text(
                "port_telemetry frames=30 full=1 dirty=29 flushes=58 converted_pixels=900000 "
                "packed_bytes=1800000 frame_ms_avg=36.5 frame_ms_max=57.0 "
                "dma_wait_ms_avg=5.2 dma_wait_ms_max=11.0 "
                "flush_done_ms_avg=13.5 flush_done_ms_max=27.0 "
                "internal_ram_peak=330000 psram_peak=120000\n",
                encoding="utf-8",
            )
            jellyframe_cli.merge_port_telemetry_report(report_path, telemetry_log)
            report = json.loads(report_path.read_text(encoding="utf-8"))

        summary = report["performanceSummary"]
        self.assertEqual(report["portTelemetry"]["summary"]["frames"], 30)
        self.assertEqual(summary["source"], "package-preflight-estimate+port-telemetry")
        self.assertEqual(summary["measuredPortFrameCount"], 30)
        self.assertEqual(summary["measuredPortFullFrameCount"], 1)
        self.assertEqual(summary["measuredPortDirtyFrameCount"], 29)
        self.assertEqual(summary["measuredPortFlushCount"], 58)
        self.assertEqual(summary["measuredPortConvertedPixels"], 900000)
        self.assertEqual(summary["measuredPortPackedBytes"], 1800000)
        self.assertEqual(summary["measuredPortAverageFrameMs"], 36.5)
        self.assertEqual(summary["measuredPortMaxFrameMs"], 57.0)
        self.assertEqual(summary["measuredPortAverageDmaWaitMs"], 5.2)
        self.assertEqual(summary["measuredPortAverageFlushDoneMs"], 13.5)
        self.assertEqual(summary["measuredPortInternalRamPeakBytes"], 330000)
        codes = {entry["code"] for entry in report["performanceAdvice"]}
        self.assertIn("performance-port-frame-time-high", codes)
        self.assertIn("performance-port-dma-wait-high", codes)
        self.assertIn("performance-port-flush-done-high", codes)
        self.assertIn("performance-port-internal-ram-high", codes)

    def test_port_telemetry_json_accepts_camel_case_metrics(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-port-telemetry-json-") as directory:
            root = Path(directory)
            report_path = root / "report.json"
            telemetry_log = root / "port.json"
            telemetry_log.write_text(json.dumps({
                "format": "jellyframe.port.telemetry.metrics.v0",
                "metrics": {
                    "frames": 12,
                    "fullFrames": 1,
                    "dirtyFrames": 11,
                    "averageFrameMs": 18.25,
                    "maxFrameMs": 24.0,
                    "averageDmaWaitMs": 1.5,
                    "averageFlushDoneMs": 4.0,
                    "internalRamPeakBytes": 128000,
                },
            }), encoding="utf-8")
            jellyframe_cli.merge_port_telemetry_report(report_path, telemetry_log)
            report = json.loads(report_path.read_text(encoding="utf-8"))

        self.assertEqual(report["portTelemetry"]["summary"]["frames"], 12)
        self.assertEqual(report["performanceSummary"]["measuredPortAverageFrameMs"], 18.25)
        self.assertNotIn("performanceAdvice", report)

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
