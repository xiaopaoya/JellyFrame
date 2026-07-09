#!/usr/bin/env python3
import argparse
import json
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import app_registry


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def default_build_dir() -> Path:
    return repo_root() / "build" / "Release"


def exe_name(name: str) -> str:
    return f"{name}.exe" if sys.platform.startswith("win") else name


def tool_path(build_dir: Path, name: str) -> Path:
    return build_dir / exe_name(name)


def run_command(command: list[str]) -> int:
    print("+ " + " ".join(command), flush=True)
    return subprocess.call(command)


def ensure_tool(path: Path) -> None:
    if not path.is_file():
        raise SystemExit(f"missing tool: {path}")


def package_script() -> Path:
    path = repo_root() / "tools" / "package_app.py"
    if not path.is_file():
        raise SystemExit(f"missing package script: {path}")
    return path


def schema_path() -> Path:
    path = repo_root() / "tools" / "schemas" / "jellyframe.app.schema.json"
    if not path.is_file():
        raise SystemExit(f"missing schema: {path}")
    return path


def target_presets_dir() -> Path:
    return repo_root() / "tools" / "presets" / "targets"


def app_templates_dir() -> Path:
    return repo_root() / "tools" / "templates" / "apps"


def load_target_config(target: str) -> dict:
    if not target:
        return {}
    path = target_presets_dir() / f"{target}.json"
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding="utf-8-sig"))


def merge_dict(base: dict, overlay: dict) -> dict:
    merged = dict(base)
    for key, value in overlay.items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key] = merge_dict(merged[key], value)
        else:
            merged[key] = value
    return merged


def load_manifest_target(root: Path, target: str) -> dict:
    if not target:
        return {}
    manifest_path = root / "jellyframe.app.json"
    if not manifest_path.is_file():
        return {}
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    manifest_targets = manifest.get("targets", {})
    if not isinstance(manifest_targets, dict):
        return {}
    target_config = manifest_targets.get(target, {})
    return target_config if isinstance(target_config, dict) else {}


def effective_target_config(root: Path, target: str) -> dict:
    preset = load_target_config(target)
    manifest_target = load_manifest_target(root, target)
    if target and not preset and not manifest_target:
        raise SystemExit(f"target is not declared by manifest and no preset exists: {target}")
    config = merge_dict(preset, manifest_target)
    if target:
        config["id"] = target
    return config


def list_target_presets() -> list[dict]:
    directory = target_presets_dir()
    if not directory.is_dir():
        return []
    presets = []
    for path in sorted(directory.glob("*.json")):
        presets.append(json.loads(path.read_text(encoding="utf-8-sig")))
    return presets


def list_target_ids() -> list[str]:
    ids = []
    for preset in list_target_presets():
        target_id = preset.get("id")
        if isinstance(target_id, str) and target_id:
            ids.append(target_id)
    return ids


def parse_targets_arg(value: str) -> list[str]:
    targets = []
    for item in value.split(","):
        target = item.strip()
        if target and target not in targets:
            targets.append(target)
    return targets


def requested_targets(args: argparse.Namespace) -> list[str]:
    if getattr(args, "all_targets", False):
        targets = list_target_ids()
    else:
        targets = parse_targets_arg(getattr(args, "targets", "") or "")
    return targets


def list_app_templates() -> list[str]:
    directory = app_templates_dir()
    if not directory.is_dir():
        return []
    return sorted(path.name for path in directory.iterdir() if path.is_dir())


def validate_app_id(value: str) -> None:
    if re.match(r"^[a-zA-Z0-9][a-zA-Z0-9_.-]*$", value):
        return
    raise SystemExit(f"invalid app id: {value}")


def package_command(args: argparse.Namespace, validate_only: bool) -> list[str]:
    command = [
        sys.executable,
        str(package_script()),
        "--root",
        str(args.root),
        "--report",
        str(args.report),
    ]
    if validate_only:
        command.append("--validate-only")
    else:
        if getattr(args, "output_cpp", None):
            command.extend(["--output-cpp", str(args.output_cpp)])
        if getattr(args, "output_bundle", None):
            command.extend(["--output-bundle", str(args.output_bundle)])
        if args.debug_dir:
            command.extend(["--debug-dir", str(args.debug_dir)])
    if args.namespace:
        command.extend(["--namespace", args.namespace])
    if args.include:
        command.extend(["--include", args.include])
    if args.target:
        command.extend(["--target", args.target])
    return command


def cmd_validate(args: argparse.Namespace) -> int:
    return run_command(package_command(args, True))


def should_run_font_resource_check(args: argparse.Namespace) -> bool:
    if getattr(args, "skip_check", False) or getattr(args, "no_font_check", False):
        return False
    return True


def effective_font_budget(args: argparse.Namespace) -> str:
    font_budget = getattr(args, "font_budget", None)
    return font_budget if font_budget else "16x16"


def load_json_if_exists(path: Path) -> dict:
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding="utf-8-sig"))


def parse_metric_value(value: str) -> int | str:
    cleaned = value.rstrip("%")
    if re.fullmatch(r"-?\d+", cleaned):
        return int(cleaned)
    return cleaned


def parse_port_metric_value(value: str) -> int | float | str:
    cleaned = value.rstrip("%")
    if re.fullmatch(r"-?\d+", cleaned):
        return int(cleaned)
    if re.fullmatch(r"-?\d+\.\d+", cleaned):
        return float(cleaned)
    return cleaned


def metric_number(metrics: dict, *keys: str) -> int | float:
    for key in keys:
        value = metrics.get(key)
        if isinstance(value, (int, float)):
            return value
    return 0


def metric_int(metrics: dict, *keys: str) -> int:
    return int(metric_number(metrics, *keys) or 0)


def metric_float(metrics: dict, *keys: str) -> float:
    return float(metric_number(metrics, *keys) or 0.0)


def parse_runtime_capture_log(log_path: Path) -> dict:
    if not log_path.is_file():
        raise SystemExit(f"missing runtime log: {log_path}")
    metrics: dict = {
        "format": "jellyframe.runtime.capture.metrics.v0",
        "source": str(log_path),
        "presentEstimateRgb565": {},
        "frameUpdate": {},
        "frameRepaint": {},
        "scrollBlits": {},
        "loadTelemetry": {},
    }
    key_map = {
        "present_estimate_rgb565": "presentEstimateRgb565",
        "frame_update": "frameUpdate",
        "frame_repaint": "frameRepaint",
        "scroll_blits": "scrollBlits",
        "load_telemetry": "loadTelemetry",
        "frame_policy_samples": "framePolicySamples",
        "host_completion_batches": "hostCompletionBatches",
        "system_event_batches": "systemEventBatches",
        "app_budget": "appBudget",
        "app_budget_script": "appBudgetScript",
    }
    for raw_line in log_path.read_text(encoding="utf-8-sig").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("[") or line.startswith("diagnostics:"):
            continue
        match = re.match(r"^(?P<name>[a-z0-9_]+)\s+(?P<body>.+)$", line)
        if not match:
            continue
        mapped = key_map.get(match.group("name"))
        if not mapped:
            continue
        parsed: dict = {}
        for token in match.group("body").split():
            if "=" not in token:
                continue
            key, value = token.split("=", 1)
            parsed[key] = parse_metric_value(value)
        metrics[mapped] = parsed
    present = metrics.get("presentEstimateRgb565", {}) if isinstance(metrics.get("presentEstimateRgb565", {}), dict) else {}
    load_telemetry = metrics.get("loadTelemetry", {}) if isinstance(metrics.get("loadTelemetry", {}), dict) else {}
    frame_update = metrics.get("frameUpdate", {}) if isinstance(metrics.get("frameUpdate", {}), dict) else {}
    scroll_blits = metrics.get("scrollBlits", {}) if isinstance(metrics.get("scrollBlits", {}), dict) else {}
    metrics["summary"] = {
        "frames": int(present.get("frames", 0) or 0),
        "fullFrames": int(present.get("full", 0) or 0),
        "dirtyFrames": int(present.get("dirty", 0) or 0),
        "flushes": int(present.get("flushes", 0) or 0),
        "convertedPixels": int(present.get("converted_pixels", 0) or 0),
        "packedBytes": int(present.get("packed_bytes", 0) or 0),
        "scrollCopiedPixels": int(scroll_blits.get("copied_pixels", 0) or 0),
        "loadSamples": int(load_telemetry.get("samples", 0) or 0),
        "loadOverloaded": int(load_telemetry.get("overloaded", 0) or 0),
        "loadDropAnimation": int(load_telemetry.get("drop_animation", 0) or 0),
        "frameUpdateRepaint": int(frame_update.get("repaint", 0) or 0),
        "maxDirtyPercent": int(load_telemetry.get("max_dirty", 0) or 0),
    }
    return metrics


def merge_runtime_capture_report(package_report_path: Path, runtime_log_path: Path) -> None:
    metrics = parse_runtime_capture_log(runtime_log_path)
    report = load_json_if_exists(package_report_path)
    if not report:
        report = {
            "format": "jellyframe.package.report",
        }
    report["runtimeMetrics"] = metrics
    write_json_report(package_report_path, report)


PORT_TELEMETRY_ALIASES = {
    "frames": "frames",
    "frame_count": "frames",
    "full": "fullFrames",
    "full_frames": "fullFrames",
    "dirty": "dirtyFrames",
    "dirty_frames": "dirtyFrames",
    "flushes": "flushes",
    "flush_count": "flushes",
    "converted_pixels": "convertedPixels",
    "packed_bytes": "packedBytes",
    "flush_bytes": "packedBytes",
    "frame_ms_avg": "averageFrameMs",
    "avg_frame_ms": "averageFrameMs",
    "frame_ms_max": "maxFrameMs",
    "max_frame_ms": "maxFrameMs",
    "dma_wait_ms_avg": "averageDmaWaitMs",
    "avg_dma_wait_ms": "averageDmaWaitMs",
    "dma_wait_ms_max": "maxDmaWaitMs",
    "max_dma_wait_ms": "maxDmaWaitMs",
    "flush_done_ms_avg": "averageFlushDoneMs",
    "avg_flush_done_ms": "averageFlushDoneMs",
    "flush_done_ms_max": "maxFlushDoneMs",
    "max_flush_done_ms": "maxFlushDoneMs",
    "internal_ram_peak": "internalRamPeakBytes",
    "internal_ram_peak_bytes": "internalRamPeakBytes",
    "psram_peak": "psramPeakBytes",
    "psram_peak_bytes": "psramPeakBytes",
}


def normalize_port_telemetry_values(raw: dict) -> dict:
    metrics: dict = {}
    for key, value in raw.items():
        mapped = PORT_TELEMETRY_ALIASES.get(str(key), str(key))
        if isinstance(value, str):
            metrics[mapped] = parse_port_metric_value(value)
        else:
            metrics[mapped] = value
    return metrics


def parse_port_telemetry_log(log_path: Path) -> dict:
    if not log_path.is_file():
        raise SystemExit(f"missing port telemetry: {log_path}")
    text = log_path.read_text(encoding="utf-8-sig")
    source = str(log_path)
    json_error = ""
    try:
        loaded = json.loads(text)
        if not isinstance(loaded, dict):
            raise ValueError("port telemetry JSON root must be an object")
        raw_metrics = loaded.get("metrics", loaded)
        if not isinstance(raw_metrics, dict):
            raise ValueError("port telemetry metrics must be an object")
        metrics = normalize_port_telemetry_values(raw_metrics)
    except json.JSONDecodeError:
        metrics = {}
        for raw_line in text.splitlines():
            line = raw_line.strip()
            if not line or line.startswith("#") or line.startswith("["):
                continue
            match = re.match(r"^(?:port_telemetry|jellyframe_port_telemetry)\s+(?P<body>.+)$", line)
            if not match:
                continue
            parsed = {}
            for token in match.group("body").split():
                if "=" not in token:
                    continue
                key, value = token.split("=", 1)
                parsed[key] = parse_port_metric_value(value)
            metrics.update(normalize_port_telemetry_values(parsed))
    except ValueError as error:
        json_error = str(error)
        metrics = {}
    if not metrics:
        if json_error:
            raise SystemExit(f"invalid port telemetry JSON: {json_error}")
        raise SystemExit(
            "port telemetry did not contain JSON metrics or a 'port_telemetry key=value ...' line"
        )
    return {
        "format": "jellyframe.port.telemetry.metrics.v0",
        "source": source,
        "summary": {
            "frames": metric_int(metrics, "frames"),
            "fullFrames": metric_int(metrics, "fullFrames"),
            "dirtyFrames": metric_int(metrics, "dirtyFrames"),
            "flushes": metric_int(metrics, "flushes"),
            "convertedPixels": metric_int(metrics, "convertedPixels"),
            "packedBytes": metric_int(metrics, "packedBytes"),
            "averageFrameMs": metric_float(metrics, "averageFrameMs"),
            "maxFrameMs": metric_float(metrics, "maxFrameMs"),
            "averageDmaWaitMs": metric_float(metrics, "averageDmaWaitMs"),
            "maxDmaWaitMs": metric_float(metrics, "maxDmaWaitMs"),
            "averageFlushDoneMs": metric_float(metrics, "averageFlushDoneMs"),
            "maxFlushDoneMs": metric_float(metrics, "maxFlushDoneMs"),
            "internalRamPeakBytes": metric_int(metrics, "internalRamPeakBytes"),
            "psramPeakBytes": metric_int(metrics, "psramPeakBytes"),
        },
        "metrics": metrics,
    }


def merge_port_telemetry_report(package_report_path: Path, port_telemetry_path: Path) -> None:
    telemetry = parse_port_telemetry_log(port_telemetry_path)
    report = load_json_if_exists(package_report_path)
    if not report:
        report = {
            "format": "jellyframe.package.report",
        }
    report["portTelemetry"] = telemetry
    write_json_report(package_report_path, report)


ADVICE_BY_CODE = {
    "visual-horizontal-overflow": {
        "title": "Content paints outside the target width",
        "explanation": "One or more boxes extend beyond the viewport on this target.",
        "action": "Use max-width: 100%, box-sizing: border-box, shorter labels, a vertical stack, or a scroll container. Check the target's paintBounds in the report.",
        "recipe": "app_author_recipes.md#narrow-targets",
    },
    "layout-text-overflow": {
        "title": "Text is wider than its layout box",
        "explanation": "The measured label cannot fit in the available box and will be clipped or visually degraded.",
        "action": "Shorten the text, increase the box width, reduce font-size, allow wrapping, or use a target-specific media rule for narrow screens.",
        "recipe": "app_author_recipes.md#narrow-targets",
    },
    "layout-text-overflow-ellipsis": {
        "title": "Ellipsis text still overflows its box",
        "explanation": "The page requested ellipsis, but the current text backend still reports a clipped or degraded narrow label.",
        "action": "Keep the text short for this target, reserve more width, or provide a narrower label through app logic or media rules.",
        "recipe": "app_author_recipes.md#narrow-targets",
    },
    "visual-scroll-needed": {
        "title": "Page content is taller than the viewport",
        "explanation": "The rendered content height exceeds the target height.",
        "action": "If scrolling is intended, put long content in an explicit overflow: auto container and allow scroll in the target gate. Otherwise reduce vertical padding, card count, or fixed heights.",
        "recipe": "app_author_recipes.md#scroll-list",
    },
    "visual-scroll-container": {
        "title": "A scroll container clips content",
        "explanation": "An internal scroll area contains more content than its visible box.",
        "action": "Make sure the container is reachable by touch/wheel/key input, keep fixed navigation outside it, and verify the target gate allows this scroll behavior.",
        "recipe": "app_author_recipes.md#scroll-list",
    },
    "visual-display-command-density": {
        "title": "Display command density is high",
        "explanation": "This page generates a dense display list for the current viewport.",
        "action": "Reduce decorative layers, shadows, gradients, generated content, or repeated nodes. Prefer one canvas/chart or one background effect over many overlapping DOM boxes.",
    },
    "visual-vertical-paint-overflow": {
        "title": "Content paints outside the target height",
        "explanation": "Paint output extends above or below the viewport. On small screens this usually means fixed positioning, negative offsets, or content that needs an explicit scroll area.",
        "action": "Move fixed or absolute elements back inside the viewport, reduce vertical spacing, or put long content in an overflow: auto container with a target gate that allows scroll.",
        "recipe": "app_author_recipes.md#scroll-list",
    },
    "style-property-unsupported": {
        "title": "CSS property is outside the supported subset",
        "explanation": "The declaration uses a property that is not implemented by the current JellyFrame CSS subset.",
        "action": "Replace it with a documented subset property from the capability matrix, or move the effect into a supported component/canvas path.",
    },
    "style-declaration-ignored": {
        "title": "CSS declaration has an unsupported value",
        "explanation": "The property is recognized, but the value is invalid or outside the supported value grammar for this subset.",
        "action": "Use a simpler documented value. For sizing, prefer px, %, min/max/clamp subsets and box-sizing: border-box.",
    },
    "style-after-property-unsupported": {
        "title": "::after uses an unsupported property",
        "explanation": "Generated content exists only as a small decoration/text subset.",
        "action": "Keep ::before/::after to content, simple box styling and low-cost decoration. Put richer styling on a real element.",
    },
    "style-after-declaration-ignored": {
        "title": "::after declaration was ignored",
        "explanation": "A generated-content declaration was parsed but its value is outside the supported pseudo-element subset.",
        "action": "Keep generated content to short text or simple decorative boxes. Use a real element or Canvas for richer decoration.",
    },
    "style-conic-gradient-unsupported": {
        "title": "conic-gradient() is outside the supported subset",
        "explanation": "JellyFrame supports only a small progress-ring-oriented conic-gradient subset.",
        "action": "Use two-color or simple stop conic gradients centered in the element, or use Canvas 2D for custom gauges.",
    },
    "style-radial-gradient-unsupported": {
        "title": "radial-gradient() is outside the supported subset",
        "explanation": "JellyFrame supports only a small center-circle radial-gradient subset for highlights and simple depth.",
        "action": "Use a two-color centered radial gradient, switch large backgrounds to linear-gradient(), or pre-render the effect as an image.",
    },
    "layer-conic-gradient-area-budget": {
        "title": "Conic gradient exceeds the paint budget",
        "explanation": "The gradient area is too expensive for the configured embedded rendering budget.",
        "action": "Shrink the element, simplify the gradient, pre-render the asset, or move the effect behind an opt-in canvas/resource budget.",
    },
    "layer-radial-gradient-area-budget": {
        "title": "Radial gradient exceeds the paint budget",
        "explanation": "Radial gradients are intended for small highlights and glows. Large areas are CPU-rasterized and can dominate paint time.",
        "action": "Keep radial gradients to small control highlights, use a linear gradient for large backgrounds, or pre-render a static asset.",
    },
    "layer-box-shadow-area-budget": {
        "title": "Box shadow exceeds the paint budget",
        "explanation": "JellyFrame paints box-shadow as a cheap approximate translucent rounded rectangle, but large expanded areas still cost pixels.",
        "action": "Keep shadows small, reduce blur/spread, or combine borders and small radial-gradient highlights for gel depth.",
    },
    "css-at-rule-skipped": {
        "title": "CSS at-rule is unsupported",
        "explanation": "The at-rule was parsed as CSS input but does not match the supported at-rule subset.",
        "action": "Use supported @media rules and simple selectors, or move target-specific choices into manifest targets and plain CSS rules.",
    },
    "css-at-rule-malformed": {
        "title": "CSS at-rule is malformed",
        "explanation": "The parser could not recover a valid at-rule from the source text.",
        "action": "Check the at-rule syntax against standard CSS grammar, then reduce it to the documented @media/keyframes subset JellyFrame supports.",
    },
    "css-at-rule-ignored": {
        "title": "CSS at-rule was ignored",
        "explanation": "The at-rule is valid CSS input, but it does not affect JellyFrame's documented app subset.",
        "action": "Remove the rule or replace it with a supported @media rule, manifest target gate, or build-time transform.",
    },
    "css-selector-skipped": {
        "title": "CSS selector is unsupported",
        "explanation": "The selector requires semantics outside the supported selector subset and the rule was not applied.",
        "action": "Use simple class, id, element, descendant or documented pseudo-class selectors. Avoid browser-only selector tricks in app UI.",
    },
    "css-rule-malformed": {
        "title": "CSS rule is malformed",
        "explanation": "The parser recovered from a CSS rule that did not match standard declaration-block syntax.",
        "action": "Fix the rule syntax first; if the rule is valid browser CSS, reduce selectors and values to the JellyFrame subset.",
    },
    "css-declaration-malformed": {
        "title": "CSS declaration is malformed",
        "explanation": "A declaration could not be parsed as a standard property/value pair.",
        "action": "Check for missing colons, semicolons, braces or unsupported nested syntax. Keep values in the documented CSS subset.",
    },
    "style-inline-declaration-malformed": {
        "title": "Inline style declaration is malformed",
        "explanation": "An element's style attribute contains a declaration the parser could not read as standard CSS.",
        "action": "Fix the inline style syntax or move the declaration into a stylesheet so the same subset rules are easier to inspect.",
    },
    "css-keyframes-malformed": {
        "title": "@keyframes rule is malformed",
        "explanation": "The animation keyframes block could not be parsed as the supported CSS keyframes subset.",
        "action": "Use simple from/to or percentage keyframes and only animate documented low-cost properties such as opacity and transform.",
    },
    "css-keyframe-malformed": {
        "title": "A keyframe selector is malformed",
        "explanation": "One keyframe entry could not be parsed as a supported from/to or percentage selector.",
        "action": "Use from, to, or numeric percentage selectors. Avoid ranges, timeline selectors or browser-only animation syntax.",
    },
    "css-keyframe-selector-ignored": {
        "title": "A keyframe selector was ignored",
        "explanation": "The keyframe selector is outside the supported animation subset.",
        "action": "Use from, to, or simple percentage selectors, and test the animation in Win32 frame-script capture.",
    },
    "css-keyframes-empty": {
        "title": "@keyframes rule has no usable frames",
        "explanation": "The animation name exists, but no supported keyframes remained after parsing.",
        "action": "Add at least two supported keyframes and keep animated properties to opacity or transform where possible.",
    },
    "script-type-unsupported": {
        "title": "Script type is unsupported",
        "explanation": "JellyFrame V0 runs bounded classic scripts, not browser modules.",
        "action": "Use classic package-local scripts and keep module bundling as a desktop build step.",
    },
    "script-load-failed": {
        "title": "Script resource could not be loaded",
        "explanation": "The script is missing, not packaged, or outside the local package resource model.",
        "action": "Reference package-local scripts only and verify they appear in the package report resources list.",
    },
    "script-loader-missing": {
        "title": "No script loader is available",
        "explanation": "The render pipeline found a script element, but this execution path was not given a script loader.",
        "action": "Run the app through the app runtime or Win32 browser when JavaScript is required; keep pseudo-browser-only checks for render-core validation.",
    },
    "script-resource-missing": {
        "title": "Script resource is missing",
        "explanation": "A package-local script path could not be resolved from the app package.",
        "action": "Fix the script src path and confirm the file appears in the package report resources list.",
    },
    "script-resource-rejected": {
        "title": "Script resource was rejected",
        "explanation": "The package loader rejected the script because it violated the resource contract or size limit.",
        "action": "Keep scripts package-local, small enough for the configured input limit, and encoded as UTF-8 text.",
    },
    "script-execution-budget-exceeded": {
        "title": "Script execution exceeded the runtime budget",
        "explanation": "The script watchdog or operation budget stopped JavaScript to protect the app host and system shell.",
        "action": "Break long work into smaller callbacks, avoid unbounded loops, reduce per-frame DOM updates, and verify with Win32 frame-script capture.",
    },
    "script-capability-missing": {
        "title": "JavaScript API is used without a manifest capability",
        "explanation": "The app script references a host-backed Web-near API, but the manifest does not request the matching JellyFrame capability.",
        "action": "Declare the reported capability in jellyframe.app.json, verify the target profile supports it, and keep a visible fallback for hosts that deny it.",
    },
    "script-host-time-ambiguous": {
        "title": "Script relies on ambient Date construction",
        "explanation": "JellyFrame V0 exposes host-injected wall-clock time through Date.now(); a no-argument Date() call is not documented as host-clock controlled.",
        "action": "Use new Date(Date.now()) when a Date object is needed, or keep Date.now() as the stored numeric timestamp.",
    },
    "script-api-deferred": {
        "title": "JavaScript API is outside the current runtime subset",
        "explanation": "The script references a browser API that is documented as deferred in the current JellyFrame JavaScript subset.",
        "action": "Use the documented V0 substitute when one exists, or move this behavior behind a desktop build step or future host/runtime capability.",
    },
    "script-api-subset": {
        "title": "JavaScript API uses only a JellyFrame subset",
        "explanation": "The script references a Web API that exists in JellyFrame, but not with full browser semantics.",
        "action": "Keep querySelector/querySelectorAll selectors to tag, .class, #id, [attr] or [attr=value]. Use explicit IDs or element references for complex relationships.",
    },
    "font-family-unmatched": {
        "title": "CSS font-family is not a manifest runtime font",
        "explanation": "A custom primary font-family was used in CSS but no manifest .jffont family matches it.",
        "action": "Use system-ui/sans-serif for the system font, or add a .jffont entry with a matching family in jellyframe.app.json.",
    },
    "font-missing-glyphs": {
        "title": "Target fonts do not cover all app text",
        "explanation": "Some source characters are not covered by the target font profile or app font supplements.",
        "action": "Run the default font subset preflight, generate a .jffont supplement from the used-chars file, then declare it in manifest fonts[].",
    },
    "missing-font-resource": {
        "title": "Declared font resource is not packaged",
        "explanation": "The manifest points at a font file that was not found in the app package.",
        "action": "Fix fonts[].source to a package-local path and rerun check/package.",
    },
    "unsupported-font-resource-format": {
        "title": "Font resource format is not runtime-loadable",
        "explanation": "Runtime app fonts must use JellyFrame .jffont resources.",
        "action": "Convert the source bitmap font to .jffont with the font pack generator, then declare the .jffont file in the manifest.",
    },
    "image-codec-target-unsupported": {
        "title": "Target does not declare this image codec",
        "explanation": "The app packages an image codec that the selected target profile may not decode.",
        "action": "Use a target-supported package image format, add a host image decoder profile, or replace large decorative images with CSS/canvas.",
    },
    "image-codec-unsupported": {
        "title": "Image codec is unsupported",
        "explanation": "The packaged image does not use a recognized V0 image codec path.",
        "action": "For current Win32/package validation, use package-local BMP or a target profile with an explicit production codec adapter.",
    },
    "manifest-capability-unknown": {
        "title": "Manifest capability is unknown",
        "explanation": "The manifest asks for a capability name JellyFrame does not recognize.",
        "action": "Use documented capability names only. Put product-specific features behind a host/profile contract before exposing them to apps.",
    },
    "service-target-unsupported": {
        "title": "Target profile does not provide a requested service",
        "explanation": "The app manifest requests a service, but the selected target reports it as unsupported.",
        "action": "Either remove the app feature for this target, choose a target profile that provides it, or degrade the UI when the service is unavailable.",
    },
    "remote-package-resource": {
        "title": "Package resource uses a remote URL",
        "explanation": "JellyFrame packages local app resources only. Runtime data requests use host services instead of remote page assets.",
        "action": "Move HTML, CSS, scripts, images and fonts into the app package. Use XHR/network capability only for bounded runtime data.",
    },
    "missing-local-resource": {
        "title": "Referenced package resource is missing",
        "explanation": "A local stylesheet, script, image, font or other resource path could not be found in the package.",
        "action": "Fix the path, keep it package-local, and rerun check/package before testing on device.",
    },
    "package-resource-missing": {
        "title": "Package resource is missing",
        "explanation": "A runtime loader asked for a package resource that is not present in the current app bundle.",
        "action": "Fix the local path, rebuild the .jfapp/debug package, and confirm the resource appears in the package report.",
    },
    "package-resource-rejected": {
        "title": "Package resource was rejected",
        "explanation": "The package loader rejected a resource because it violated local path, size, type or containment rules.",
        "action": "Keep resources package-local, avoid symlinks or escaped paths, and keep text resources below the configured source package limit.",
    },
    "package-resource-crc-mismatch": {
        "title": "Package resource integrity check failed",
        "explanation": "A .jfapp resource payload did not match its recorded CRC.",
        "action": "Rebuild or reinstall the app package. Treat this as a corrupted package or interrupted transfer until proven otherwise.",
    },
    "stylesheet-resource-missing": {
        "title": "Stylesheet resource is missing",
        "explanation": "A package-local stylesheet path could not be resolved from the app package.",
        "action": "Fix the stylesheet href path and confirm the file appears in the package report resources list.",
    },
    "stylesheet-resource-rejected": {
        "title": "Stylesheet resource was rejected",
        "explanation": "The package loader rejected the stylesheet because it violated the resource contract or size limit.",
        "action": "Keep stylesheets package-local, small enough for the configured input limit, and encoded as UTF-8 text.",
    },
    "manifest-field-unknown": {
        "title": "Manifest contains an unknown field",
        "explanation": "The field is not part of the documented manifest contract consumed by current tooling.",
        "action": "Remove the field, move product-specific data under a documented host/profile contract, or update the schema before relying on it.",
    },
    "audio-capability-resource-mismatch": {
        "title": "Audio resource is packaged without matching capability",
        "explanation": "The app includes audio resources but the manifest/service intent does not match the current audio playback contract.",
        "action": "Declare the documented audio capability for intended playback, or remove unused audio assets from the package.",
    },
    "image-bmp-invalid": {
        "title": "BMP image header is invalid",
        "explanation": "The packaged BMP metadata could not be parsed by the lightweight package checker.",
        "action": "Regenerate the image as a simple uncompressed BMP for Win32 validation, or use a target profile with a production image codec adapter.",
    },
    "image-bmp-unsupported-debug-format": {
        "title": "BMP format is not supported by the debug shell",
        "explanation": "The Win32 debug shell only validates a small uncompressed BMP subset.",
        "action": "Use uncompressed 24-bit or 32-bit BMP for current shell validation, or validate the asset through a product image codec adapter.",
    },
    "font-axis-metadata-missing": {
        "title": "Font size/weight metadata is missing",
        "explanation": "Runtime app fonts are bitmap supplements, so package metadata should say which sizes and weights were validated.",
        "action": "Add fonts[].sizes and fonts[].weights to the manifest entry for this app font.",
    },
    "font-axis-metadata-invalid": {
        "title": "Font size/weight metadata is invalid",
        "explanation": "The manifest font metadata is present but not in the documented numeric array shape.",
        "action": "Use integer arrays such as \"sizes\": [16, 20] and \"weights\": [400, 700].",
    },
    "font-license-missing": {
        "title": "Font license metadata is missing",
        "explanation": "The package redistributes or declares a font supplement without clear license metadata.",
        "action": "Add fonts[].license.name and fonts[].license.source after confirming you can redistribute the generated font subset.",
    },
    "font-license-incomplete": {
        "title": "Font license metadata is incomplete",
        "explanation": "A font license block exists but does not include enough source/license information for release review.",
        "action": "Fill in the missing license fields, or remove the app font until its redistribution terms are clear.",
    },
    "invalid-jffont-resource": {
        "title": ".jffont resource is invalid",
        "explanation": "The declared runtime font supplement could not be parsed as a JellyFrame bitmap font resource.",
        "action": "Regenerate it with jellyframe_font_pack_gen and verify the manifest points at the generated .jffont file.",
    },
    "app-font-resource-missing": {
        "title": "App font resource is missing",
        "explanation": "A manifest-declared app font could not be loaded from the package at runtime.",
        "action": "Fix fonts[].source, rebuild the package, and confirm the .jffont resource appears in the font diagnostics.",
    },
    "app-font-resource-invalid": {
        "title": "App font resource is invalid",
        "explanation": "A manifest-declared font exists but could not be parsed as a runtime .jffont supplement.",
        "action": "Regenerate the .jffont resource and keep fonts[].sizes, weights and license metadata in sync with the generated file.",
    },
    "animation-keyframe-property-unsupported": {
        "title": "Animation targets an unsupported property",
        "explanation": "The keyframe contains a property that cannot be animated by JellyFrame's low-cost animation subset.",
        "action": "Animate opacity or transform where possible. For layout-changing animation, redesign around a smaller dirty region or use a host-driven state transition.",
    },
    "animation-keyframe-declaration-ignored": {
        "title": "Animation keyframe declaration was ignored",
        "explanation": "A keyframe declaration uses a value outside the supported animation subset.",
        "action": "Keep keyframe values simple, documented and measurable in Win32 frame-script capture.",
    },
    "animation-keyframes-empty": {
        "title": "Animation has no usable keyframes",
        "explanation": "After parsing and subset filtering, the animation timeline has no frames to play.",
        "action": "Add from/to or percentage keyframes using supported animated properties.",
    },
    "animation-keyframes-missing": {
        "title": "Animation references missing keyframes",
        "explanation": "An element requested an animation name that was not found in the parsed stylesheet keyframes.",
        "action": "Check the animation-name spelling, make sure @keyframes is packaged with the page, and keep keyframes in the supported subset.",
    },
    "animation-active-limit": {
        "title": "Too many animations are active",
        "explanation": "The page exceeds the configured active animation budget for the target.",
        "action": "Reduce concurrent animations, pause offscreen effects, or merge visual motion into one smaller transform/canvas region.",
    },
    "layer-transform-unsupported": {
        "title": "Transform is outside the compositing subset",
        "explanation": "The element uses a transform form that cannot be represented by the current layer/display-list subset.",
        "action": "Use documented translate/scale/rotate forms, simplify transform composition, or pre-render the transformed decoration.",
    },
    "paint-framebuffer-budget": {
        "title": "Framebuffer allocation exceeded the paint budget",
        "explanation": "The renderer refused a framebuffer size that would exceed the configured pixel or memory budget.",
        "action": "Reduce target framebuffer size, avoid oversized offscreen/canvas surfaces, and verify the port keeps full framebuffers out of scarce internal RAM.",
    },
    "paint-offscreen-budget": {
        "title": "Offscreen paint surface exceeded the budget",
        "explanation": "A clipped, shadowed, transformed or composited path requested too much temporary pixel memory.",
        "action": "Shrink the effect area, reduce shadow/gradient/canvas size, or pre-render static art when it is cheaper than runtime offscreen paint.",
    },
    "paint-image-fallback": {
        "title": "Image paint used a fallback path",
        "explanation": "The renderer could not draw the image with the requested fast path or decoded surface.",
        "action": "Check image dimensions, codec support, object-fit/object-position values and target image decoder capability.",
    },
    "paint-non-ascii-fallback": {
        "title": "Text used the non-ASCII fallback path",
        "explanation": "Visible text contains characters outside the current fast bitmap/system font coverage.",
        "action": "Generate and declare a .jffont supplement for the used characters, then rerun package font diagnostics.",
    },
    "paint-text-backend-failed": {
        "title": "Text backend could not paint a run",
        "explanation": "The configured text painter rejected or failed one text run.",
        "action": "Check font coverage, font size, backend availability and app .jffont declarations before testing on device.",
    },
    "image-decode-request": {
        "title": "Image decode request was queued",
        "explanation": "The host image service accepted an asynchronous decode request.",
        "action": "This is normally informational. If the image appears late, check cache state, decode completion and host image worker budget.",
    },
    "image-decode-completion": {
        "title": "Image decode completion was received",
        "explanation": "The host image service returned a decoded image result to the app runtime.",
        "action": "This is normally informational. If rendering still falls back, inspect codec, dimensions and decoded surface budget.",
    },
    "image-decode-unsupported": {
        "title": "Image codec is not supported by the host",
        "explanation": "The app asked the host image service to decode a format this target does not provide.",
        "action": "Use a target-supported codec, add a product image decoder adapter, or replace the asset with a package-local supported image.",
    },
    "image-decode-invalid": {
        "title": "Image data is invalid",
        "explanation": "The host image decoder could not parse the resource as the requested image format.",
        "action": "Regenerate the image asset and verify package image diagnostics before testing on device.",
    },
    "image-decode-budget": {
        "title": "Image decode exceeded the host budget",
        "explanation": "The decoded image would exceed the target's pixel, byte or cache budget.",
        "action": "Reduce image dimensions, compress/subset assets, or use CSS/canvas decoration when it is cheaper for the target.",
    },
    "image-cache-state": {
        "title": "Image cache state changed",
        "explanation": "The host image cache reported its debug state for this app.",
        "action": "Use this as telemetry when diagnosing late images or cache churn; it is not usually a release-blocking issue.",
    },
    "image-cache-stale-entry": {
        "title": "Image cache entry became stale",
        "explanation": "A cached decoded surface no longer matches the current app/resource generation.",
        "action": "This should recover automatically. If it repeats, check app reload/rebind paths and resource versioning.",
    },
    "system-event-rejected": {
        "title": "System event was rejected",
        "explanation": "A host or debug script tried to inject a system event that was not accepted by the current runtime policy or queue budget.",
        "action": "Check event type, target app instance, queue capacity and foreground/background policy before relying on this event.",
    },
    "html-unmatched-end-tag": {
        "title": "HTML parser recovered from an unmatched end tag",
        "explanation": "The markup contains a closing tag that did not match the open element stack.",
        "action": "Fix the HTML structure. Browser recovery may hide this on desktop, but JellyFrame apps should keep markup small and explicit.",
    },
    "html-non-void-self-closing": {
        "title": "HTML parser recovered from a non-void self-closing tag",
        "explanation": "A non-void HTML element was written with self-closing syntax.",
        "action": "Use explicit start and end tags for non-void elements, for example <div></div> instead of <div />.",
    },
    "html-empty-tag-name": {
        "title": "HTML parser found an empty tag name",
        "explanation": "The tokenizer encountered markup that does not form a valid HTML tag name.",
        "action": "Check nearby '<' and '>' characters and escape literal comparison signs in text.",
    },
    "html-element-unsupported": {
        "title": "HTML element is outside the app subset",
        "explanation": "The markup uses a browser/platform element that JellyFrame does not implement as a real embedded app feature.",
        "action": "Replace iframe/embed/object/slot/image-map style markup with package-local images, explicit buttons, Canvas, or host-owned services.",
    },
    "html-form-submit-deferred": {
        "title": "Browser form submission is not implemented",
        "explanation": "JellyFrame renders form controls, but it does not run browser navigation or HTTP form submission.",
        "action": "Handle the form action in app JavaScript using supported control APIs and an allowed host service such as XMLHttpRequest GET V0.",
    },
    "target-gate-not-accepted": {
        "title": "Target gate is not accepted",
        "explanation": "The app declared this target as publish-gated and the responsive profile violates that gate.",
        "action": "Open the target's responsive profile, fix the listed overflow/scroll/diagnostic reasons, or intentionally lower the gate policy to warn while the target is still experimental.",
        "recipe": "app_author_recipes.md#validation",
    },
}


ADVICE_IGNORED_CODES = {
    "css-media-query-not-matched",
}


def advice_template_for_code(code: str) -> dict:
    if not code or code in ADVICE_IGNORED_CODES:
        return {}
    if code in ADVICE_BY_CODE:
        return ADVICE_BY_CODE[code]
    if "budget" in code or code.endswith("-limit"):
        return {
            "title": "A runtime or rendering budget was reached",
            "explanation": "The app is asking for more nodes, resources, handles, commands or pixels than the configured target budget allows.",
            "action": "Simplify the page, reduce repeated UI, shrink assets, or raise the manifest/target budget only after measuring the device cost.",
        }
    if "capability" in code or code.endswith("-denied"):
        return {
            "title": "A capability or host service is unavailable",
            "explanation": "The app used an API or resource path that must be allowed by both manifest and target host profile.",
            "action": "Declare the documented manifest capability, verify the selected target supports it, and provide a visible fallback when the host denies it.",
        }
    return {
        "title": "Diagnostic needs app author review",
        "explanation": "The toolchain reported a compatibility, resource, budget or degradation diagnostic for this app.",
        "action": "Inspect this entry's source/detail, then check the app author guide and capability matrix. If it is intentional, document the tradeoff; otherwise simplify the app or use a documented subset feature.",
    }


def parse_diagnostic_detail(detail: str) -> dict:
    if not detail or "=" not in detail:
        return {}
    parsed: dict[str, object] = {}
    try:
        tokens = shlex.split(detail)
    except ValueError:
        tokens = detail.split()
    for token in tokens:
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        if not key:
            continue
        if re.fullmatch(r"-?\d+", value):
            parsed[key] = int(value)
        else:
            parsed[key] = value
    return parsed


def diagnostic_metrics_from_detail(parsed: dict) -> dict:
    keys = (
        "measuredWidth",
        "availableWidth",
        "contentWidth",
        "textIndent",
        "fontSize",
        "fontWeight",
        "overflowLeft",
        "overflowRight",
        "boxLeft",
        "boxRight",
        "boxWidth",
        "boxOverflowLeft",
        "boxOverflowRight",
        "boxTop",
        "boxBottom",
        "boxOverflowTop",
        "boxOverflowBottom",
        "boxHeight",
        "contentHeight",
        "overflowY",
        "viewportHeight",
    )
    metrics = {key: parsed[key] for key in keys if key in parsed}
    return metrics


def detail_location(parsed: dict) -> str:
    node = str(parsed.get("node", "") or "")
    path = str(parsed.get("path", "") or "")
    if node and path:
        return f"{node} ({path})"
    return node or path


def overflow_phrase(parsed: dict, negative_key: str, positive_key: str,
                    negative_label: str, positive_label: str) -> str:
    parts = []
    negative = parsed.get(negative_key)
    positive = parsed.get(positive_key)
    if isinstance(negative, int) and negative > 0:
        parts.append(f"{negative}px {negative_label}")
    if isinstance(positive, int) and positive > 0:
        parts.append(f"{positive}px {positive_label}")
    return " and ".join(parts)


def specialize_developer_advice(entry: dict, code: str, parsed: dict) -> None:
    location = detail_location(parsed)
    if code in {"layout-text-overflow", "layout-text-overflow-ellipsis"}:
        text = str(parsed.get("text", "") or "")
        measured = parsed.get("measuredWidth")
        available = parsed.get("availableWidth") or parsed.get("contentWidth")
        if text and isinstance(measured, int) and isinstance(available, int):
            entry["action"] = (
                f'Text "{text}" measures {measured}px but only {available}px is available. '
                "Shorten the label, reserve more width, reduce font-size, allow wrapping, "
                "or use a target-specific media rule."
            )
        elif location:
            entry["action"] = (
                f"Check {location}; its text is wider than the layout box. "
                "Shorten the label, reserve more width, reduce font-size, allow wrapping, "
                "or use a target-specific media rule."
            )
        return

    if code == "visual-horizontal-overflow":
        overflow = overflow_phrase(parsed, "boxOverflowLeft", "boxOverflowRight",
                                   "past the left edge", "past the right edge")
        if overflow:
            subject = location or "The likely source element"
            entry["action"] = (
                f"{subject} paints {overflow}. Use max-width: 100%, box-sizing: border-box, "
                "shorter labels, a vertical stack, or an explicit scroll container."
            )
        return

    if code == "visual-vertical-paint-overflow":
        overflow = overflow_phrase(parsed, "boxOverflowTop", "boxOverflowBottom",
                                   "above the viewport", "below the viewport")
        if overflow:
            subject = location or "The likely source element"
            entry["action"] = (
                f"{subject} paints {overflow}. Move fixed or absolute elements back inside "
                "the viewport, reduce vertical spacing, or put long content in an "
                "overflow: auto container."
            )
        return

    if code == "visual-scroll-container":
        box_height = parsed.get("boxHeight")
        content_height = parsed.get("contentHeight")
        overflow_y = parsed.get("overflowY")
        if isinstance(box_height, int) and isinstance(content_height, int):
            subject = location or "This scroll container"
            suffix = f" with {overflow_y}px clipped" if isinstance(overflow_y, int) else ""
            entry["action"] = (
                f"{subject} shows {box_height}px of {content_height}px content{suffix}. "
                "Make sure it is reachable by touch/wheel/key input, keep fixed navigation "
                "outside it, and verify the target gate allows scroll."
            )
        return

    if code == "visual-scroll-needed":
        content_height = parsed.get("contentHeight")
        viewport_height = parsed.get("viewportHeight")
        if isinstance(content_height, int) and isinstance(viewport_height, int):
            entry["action"] = (
                f"The page content is {content_height}px tall in a {viewport_height}px target. "
                "If scrolling is intended, put long content in an explicit overflow: auto "
                "container and allow scroll in the target gate; otherwise reduce vertical "
                "padding, card count, or fixed heights."
            )


def ratio_percent(used: int, limit: int) -> int:
    if limit <= 0:
        return 0
    return int((max(0, used) * 100 + limit - 1) // limit)


def perf_score_for_profile(profile: dict) -> int:
    pipeline = profile.get("pipeline", {}) if isinstance(profile.get("pipeline", {}), dict) else {}
    layout = profile.get("layout", {}) if isinstance(profile.get("layout", {}), dict) else {}
    viewport = profile.get("viewport", {}) if isinstance(profile.get("viewport", {}), dict) else {}
    width = int(viewport.get("width", 0) or 0)
    height = int(viewport.get("height", 0) or 0)
    area = max(1, width * height)
    display_commands = int(pipeline.get("displayCommands", 0) or 0)
    layers = int(pipeline.get("layers", 0) or 0)
    estimated_heap = int(pipeline.get("estimatedHeapBytes", 0) or 0)
    framebuffer_bytes = int(pipeline.get("framebufferBytes", 0) or 0)
    score = 0
    if estimated_heap > 768 * 1024:
        score += 3
    elif estimated_heap > 512 * 1024:
        score += 2
    elif estimated_heap > 384 * 1024:
        score += 1
    if framebuffer_bytes > 360000:
        score += 2
    elif framebuffer_bytes > 220160:
        score += 1
    if display_commands > max(96, area // 900):
        score += 2
    elif display_commands > max(64, area // 1400):
        score += 1
    if layers > 24:
        score += 2
    elif layers > 12:
        score += 1
    if bool(layout.get("horizontalOverflow", False)):
        score += 1
    return score


def perf_rating(score: int) -> str:
    if score >= 6:
        return "high-risk"
    if score >= 3:
        return "watch"
    return "ok"


def top_timing_stage(timings: dict) -> dict:
    ignored = {"total"}
    top_name = ""
    top_us = 0
    for key, value in timings.items():
        if key in ignored:
            continue
        try:
            micros = int(value or 0)
        except (TypeError, ValueError):
            continue
        if micros > top_us:
            top_name = str(key)
            top_us = micros
    return {"stage": top_name, "us": top_us} if top_name else {}


def timing_stage_guidance(stage: str) -> dict:
    guidance = {
        "parse": {
            "title": "HTML/CSS parse time is the largest measured stage",
            "explanation": "The desktop preflight spent most of its pipeline time parsing source text.",
            "action": "Reduce large inline styles/scripts, split repeated markup into generated data at build time, and keep selectors simple.",
        },
        "style": {
            "title": "Style resolution is the largest measured stage",
            "explanation": "The desktop preflight spent most of its pipeline time matching selectors and applying declarations.",
            "action": "Prefer class/id selectors, reduce selector fan-out, and avoid many nearly identical rules over large node sets.",
        },
        "layout": {
            "title": "Layout is the largest measured stage",
            "explanation": "The desktop preflight spent most of its pipeline time computing box sizes and positions.",
            "action": "Reduce nested layout, avoid over-constrained fixed sizes, keep flex/grid structures shallow, and use explicit scroll containers for long lists.",
        },
        "layer": {
            "title": "Layer building is the largest measured stage",
            "explanation": "The desktop preflight spent most of its pipeline time building layers and display commands.",
            "action": "Reduce positioned or overlapping decoration, collapse repeated effects, and keep generated content small.",
        },
        "paint": {
            "title": "Paint is the largest measured stage",
            "explanation": "The desktop preflight spent most of its pipeline time rasterizing pixels.",
            "action": "Shrink dirty areas, reduce large gradients/shadows/canvas paths, avoid full-frame repaint during animation, and pre-render static heavy art when it is cheaper.",
        },
        "present": {
            "title": "Present is the largest measured stage",
            "explanation": "The desktop preflight spent most of its pipeline time preparing the framebuffer for display.",
            "action": "Keep updates on dirty rectangles or scroll-strip paths, reduce full-frame repaint, and confirm panel/DMA costs with port telemetry.",
        },
    }
    return guidance.get(stage, {
        "title": "A measured pipeline stage dominates preflight time",
        "explanation": "The desktop preflight reports one pipeline stage as the largest measured cost.",
        "action": "Inspect the stage name and compare it with DOM size, display commands, dirty area and port telemetry before optimizing.",
    })


def append_bottleneck(bottlenecks: list[dict],
                      seen: set[tuple[str, str]],
                      code: str,
                      title: str,
                      target: str = "",
                      metrics: dict | None = None) -> None:
    key = (code, target)
    if key in seen:
        return
    seen.add(key)
    entry = {
        "code": code,
        "title": title,
    }
    if target:
        entry["target"] = target
    if metrics:
        entry["metrics"] = metrics
    bottlenecks.append(entry)


def performance_bottlenecks(summary: dict) -> list[dict]:
    bottlenecks: list[dict] = []
    seen: set[tuple[str, str]] = set()
    if int(summary.get("score", 0) or 0) >= 3:
        append_bottleneck(
            bottlenecks, seen, "performance-risk-score",
            "Preflight score says this app needs measurement on target hardware.",
            "", {"score": int(summary.get("score", 0) or 0), "rating": str(summary.get("rating", "unknown"))})
    slow_stage = summary.get("slowestMeasuredStage", {})
    if isinstance(slow_stage, dict) and int(slow_stage.get("us", 0) or 0) >= 5000:
        stage = str(slow_stage.get("stage", ""))
        append_bottleneck(
            bottlenecks, seen, f"performance-stage-{stage or 'unknown'}",
            timing_stage_guidance(stage).get("title", "A measured pipeline stage dominates preflight time."),
            "", {"stage": stage, "us": int(slow_stage.get("us", 0) or 0)})
    if int(summary.get("fullFramePresentTargets", 0) or 0) > 0:
        append_bottleneck(
            bottlenecks, seen, "performance-full-frame-present",
            "At least one target needs a full-frame present in preflight.",
            "", {"targets": int(summary.get("fullFramePresentTargets", 0) or 0)})
    for target in summary.get("perTarget", []):
        if not isinstance(target, dict):
            continue
        name = str(target.get("target", "default"))
        if int(target.get("displayCommands", 0) or 0) > 96:
            append_bottleneck(
                bottlenecks, seen, "performance-display-command-count",
                "This target has a large display command list.",
                name, {"displayCommands": int(target.get("displayCommands", 0) or 0)})
        if int(target.get("layers", 0) or 0) > 16:
            append_bottleneck(
                bottlenecks, seen, "performance-layer-count",
                "This target builds many layers.",
                name, {"layers": int(target.get("layers", 0) or 0)})
        if int(target.get("estimatedHeapBytes", 0) or 0) > 512 * 1024:
            append_bottleneck(
                bottlenecks, seen, "performance-pipeline-heap-estimate",
                "This target has a high estimated pipeline heap.",
                name, {"estimatedHeapBytes": int(target.get("estimatedHeapBytes", 0) or 0)})
    if int(summary.get("measuredLoadOverloadedFrames", 0) or 0) > 0:
        append_bottleneck(
            bottlenecks, seen, "performance-runtime-overloaded",
            "Runtime capture reported frames over budget.",
            "", {"frames": int(summary.get("measuredLoadOverloadedFrames", 0) or 0)})
    if int(summary.get("measuredMaxDirtyPercent", 0) or 0) >= 90:
        append_bottleneck(
            bottlenecks, seen, "performance-dirty-area-high",
            "Runtime capture reports a near-full-screen dirty area.",
            "", {"maxDirtyPercent": int(summary.get("measuredMaxDirtyPercent", 0) or 0)})
    if float(summary.get("measuredPortAverageFrameMs", 0) or 0) > 33.4:
        append_bottleneck(
            bottlenecks, seen, "performance-port-frame-time-high",
            "Real-device average frame time is above a 30 FPS budget.",
            "", {"averageFrameMs": float(summary.get("measuredPortAverageFrameMs", 0) or 0)})
    if float(summary.get("measuredPortAverageFlushDoneMs", 0) or 0) > 12.0:
        append_bottleneck(
            bottlenecks, seen, "performance-port-flush-done-high",
            "Panel flush completion is a likely real-device bottleneck.",
            "", {"averageFlushDoneMs": float(summary.get("measuredPortAverageFlushDoneMs", 0) or 0)})
    return bottlenecks[:8]


def append_performance_advice(advice: list[dict],
                              seen: set[tuple[str, str]],
                              code: str,
                              severity: str,
                              title: str,
                              explanation: str,
                              action: str,
                              target: str = "",
                              metrics: dict | None = None) -> None:
    key = (code, target)
    if key in seen:
        return
    seen.add(key)
    entry = {
        "code": code,
        "severity": severity,
        "title": title,
        "explanation": explanation,
        "action": action,
    }
    if target:
        entry["target"] = target
    if metrics:
        entry["metrics"] = metrics
    advice.append(entry)


def collect_performance_summary(report: dict) -> dict:
    profiles = report.get("responsiveProfiles", [])
    if not isinstance(profiles, list) or not profiles:
        pipeline = report.get("pipelineDiagnostics", {}).get("pipeline", {}) \
            if isinstance(report.get("pipelineDiagnostics", {}), dict) else {}
        if isinstance(pipeline, dict) and pipeline:
            profiles = [{
                "target": str(report.get("target", {}).get("id", "default"))
                if isinstance(report.get("target", {}), dict) else "default",
                "pipeline": {
                    "domNodes": int(pipeline.get("domNodes", 0) or 0),
                    "renderObjects": int(pipeline.get("renderObjects", 0) or 0),
                    "layoutBoxes": int(pipeline.get("layoutBoxes", 0) or 0),
                    "layers": int(pipeline.get("layers", 0) or 0),
                    "displayCommands": int(pipeline.get("displayCommands", 0) or 0),
                    "framebufferBytes": int(pipeline.get("framebufferBytes", 0) or 0),
                    "estimatedHeapBytes": int(pipeline.get("estimatedHeapBytes", 0) or 0),
                },
                "layout": report.get("pipelineDiagnostics", {}).get("layout", {})
                if isinstance(report.get("pipelineDiagnostics", {}), dict) else {},
                "viewport": report.get("pipelineDiagnostics", {}).get("viewport", {})
                if isinstance(report.get("pipelineDiagnostics", {}), dict) else {},
                "frameUpdate": report.get("pipelineDiagnostics", {}).get("frameUpdate", {})
                if isinstance(report.get("pipelineDiagnostics", {}), dict) else {},
                "timingsUs": report.get("pipelineDiagnostics", {}).get("timingsUs", {})
                if isinstance(report.get("pipelineDiagnostics", {}), dict) else {},
            }]

    per_target = []
    max_score = 0
    max_heap = 0
    max_framebuffer = 0
    max_display_commands = 0
    max_layers = 0
    max_dom_nodes = 0
    max_total_us = 0
    slowest_stage: dict = {}
    full_frame_targets = 0
    for profile in profiles:
        if not isinstance(profile, dict):
            continue
        pipeline = profile.get("pipeline", {}) if isinstance(profile.get("pipeline", {}), dict) else {}
        frame_update = profile.get("frameUpdate", {}) if isinstance(profile.get("frameUpdate", {}), dict) else {}
        timings = profile.get("timingsUs", {}) if isinstance(profile.get("timingsUs", {}), dict) else {}
        score = perf_score_for_profile(profile)
        max_score = max(max_score, score)
        dom_nodes = int(pipeline.get("domNodes", 0) or 0)
        layers = int(pipeline.get("layers", 0) or 0)
        display_commands = int(pipeline.get("displayCommands", 0) or 0)
        framebuffer_bytes = int(pipeline.get("framebufferBytes", 0) or 0)
        estimated_heap = int(pipeline.get("estimatedHeapBytes", 0) or 0)
        max_heap = max(max_heap, estimated_heap)
        max_framebuffer = max(max_framebuffer, framebuffer_bytes)
        max_display_commands = max(max_display_commands, display_commands)
        max_layers = max(max_layers, layers)
        max_dom_nodes = max(max_dom_nodes, dom_nodes)
        total_us = int(timings.get("total", 0) or 0) if isinstance(timings, dict) else 0
        if total_us > max_total_us:
            max_total_us = total_us
            slowest_stage = top_timing_stage(timings)
        if frame_update.get("repaint") == "full-frame":
            full_frame_targets += 1
        per_target.append({
            "target": str(profile.get("target", "default")),
            "rating": perf_rating(score),
            "score": score,
            "domNodes": dom_nodes,
            "layers": layers,
            "displayCommands": display_commands,
            "framebufferBytes": framebuffer_bytes,
            "estimatedHeapBytes": estimated_heap,
            "frameUpdate": frame_update if isinstance(frame_update, dict) else {},
            "timingsUs": timings if isinstance(timings, dict) else {},
        })

    budget = report.get("runtimeBudgetEstimate", {})
    resource_budget = budget.get("resources", {}) if isinstance(budget, dict) and isinstance(budget.get("resources", {}), dict) else {}
    display_budget = budget.get("displayCommands", {}) if isinstance(budget, dict) and isinstance(budget.get("displayCommands", {}), dict) else {}
    resource_used = int(resource_budget.get("used", 0) or 0)
    resource_limit = int(resource_budget.get("limit", 0) or 0)
    display_limit = int(display_budget.get("limit", 0) or 0)
    runtime_metrics = report.get("runtimeMetrics", {}) if isinstance(report.get("runtimeMetrics", {}), dict) else {}
    runtime_summary = runtime_metrics.get("summary", {}) if isinstance(runtime_metrics.get("summary", {}), dict) else {}
    port_telemetry = report.get("portTelemetry", {}) if isinstance(report.get("portTelemetry", {}), dict) else {}
    port_summary = port_telemetry.get("summary", {}) if isinstance(port_telemetry.get("summary", {}), dict) else {}

    summary = {
        "model": "jellyframe.package.performance-summary.v0",
        "source": "package-preflight-estimate",
        "rating": perf_rating(max_score),
        "score": max_score,
        "targetCount": len(per_target),
        "maxEstimatedHeapBytes": max_heap,
        "maxFramebufferBytes": max_framebuffer,
        "maxDisplayCommands": max_display_commands,
        "maxLayers": max_layers,
        "maxDomNodes": max_dom_nodes,
        "maxTotalPipelineUs": max_total_us,
        "slowestMeasuredStage": slowest_stage,
        "fullFramePresentTargets": full_frame_targets,
        "resourceBudgetPercent": ratio_percent(resource_used, resource_limit),
        "displayCommandBudgetPercent": ratio_percent(max_display_commands, display_limit),
        "perTarget": per_target,
        "notes": [
            "This is a static preflight estimate, not measured frame time.",
            "Use Win32 frame-script capture or port telemetry for actual milliseconds, DMA wait and flush-done timing.",
        ],
    }
    if runtime_metrics:
        summary["source"] = "package-preflight-estimate+runtime-capture"
        summary["runtimeCaptureSource"] = str(runtime_metrics.get("source", ""))
        summary["measuredFrameCount"] = int(runtime_summary.get("frames", 0) or 0)
        summary["measuredFullFrameCount"] = int(runtime_summary.get("fullFrames", 0) or 0)
        summary["measuredDirtyFrameCount"] = int(runtime_summary.get("dirtyFrames", 0) or 0)
        summary["measuredFlushCount"] = int(runtime_summary.get("flushes", 0) or 0)
        summary["measuredConvertedPixels"] = int(runtime_summary.get("convertedPixels", 0) or 0)
        summary["measuredPackedBytes"] = int(runtime_summary.get("packedBytes", 0) or 0)
        summary["measuredScrollCopiedPixels"] = int(runtime_summary.get("scrollCopiedPixels", 0) or 0)
        summary["measuredLoadSamples"] = int(runtime_summary.get("loadSamples", 0) or 0)
        summary["measuredLoadOverloadedFrames"] = int(runtime_summary.get("loadOverloaded", 0) or 0)
        summary["measuredDropAnimationFrames"] = int(runtime_summary.get("loadDropAnimation", 0) or 0)
        summary["measuredMaxDirtyPercent"] = int(runtime_summary.get("maxDirtyPercent", 0) or 0)
        summary["notes"] = summary["notes"] + [
            "Runtime metrics were merged from a Win32 frame-script or capture log.",
        ]
    if port_telemetry:
        summary["source"] = summary["source"] + "+port-telemetry"
        summary["portTelemetrySource"] = str(port_telemetry.get("source", ""))
        summary["measuredPortFrameCount"] = int(port_summary.get("frames", 0) or 0)
        summary["measuredPortFullFrameCount"] = int(port_summary.get("fullFrames", 0) or 0)
        summary["measuredPortDirtyFrameCount"] = int(port_summary.get("dirtyFrames", 0) or 0)
        summary["measuredPortFlushCount"] = int(port_summary.get("flushes", 0) or 0)
        summary["measuredPortConvertedPixels"] = int(port_summary.get("convertedPixels", 0) or 0)
        summary["measuredPortPackedBytes"] = int(port_summary.get("packedBytes", 0) or 0)
        summary["measuredPortAverageFrameMs"] = float(port_summary.get("averageFrameMs", 0) or 0)
        summary["measuredPortMaxFrameMs"] = float(port_summary.get("maxFrameMs", 0) or 0)
        summary["measuredPortAverageDmaWaitMs"] = float(port_summary.get("averageDmaWaitMs", 0) or 0)
        summary["measuredPortMaxDmaWaitMs"] = float(port_summary.get("maxDmaWaitMs", 0) or 0)
        summary["measuredPortAverageFlushDoneMs"] = float(port_summary.get("averageFlushDoneMs", 0) or 0)
        summary["measuredPortMaxFlushDoneMs"] = float(port_summary.get("maxFlushDoneMs", 0) or 0)
        summary["measuredPortInternalRamPeakBytes"] = int(port_summary.get("internalRamPeakBytes", 0) or 0)
        summary["measuredPortPsramPeakBytes"] = int(port_summary.get("psramPeakBytes", 0) or 0)
        summary["notes"] = summary["notes"] + [
            "Port telemetry was merged from a real-device or board-port log.",
        ]
    bottlenecks = performance_bottlenecks(summary)
    if bottlenecks:
        summary["bottlenecks"] = bottlenecks
    return summary


def collect_performance_advice(report: dict, summary: dict) -> list[dict]:
    advice: list[dict] = []
    seen: set[tuple[str, str]] = set()
    for target in summary.get("perTarget", []):
        if not isinstance(target, dict):
            continue
        name = str(target.get("target", "default"))
        commands = int(target.get("displayCommands", 0) or 0)
        layers = int(target.get("layers", 0) or 0)
        heap = int(target.get("estimatedHeapBytes", 0) or 0)
        framebuffer = int(target.get("framebufferBytes", 0) or 0)
        timings = target.get("timingsUs", {}) if isinstance(target.get("timingsUs", {}), dict) else {}
        slow_stage = top_timing_stage(timings)
        if int(slow_stage.get("us", 0) or 0) >= 5000:
            stage = str(slow_stage.get("stage", ""))
            guidance = timing_stage_guidance(stage)
            append_performance_advice(
                advice, seen, f"performance-stage-{stage or 'unknown'}", "info",
                guidance["title"],
                guidance["explanation"],
                guidance["action"],
                name, {"stage": stage, "us": int(slow_stage.get("us", 0) or 0)})
        if heap > 512 * 1024:
            append_performance_advice(
                advice, seen, "performance-pipeline-heap-estimate", "warning",
                "Pipeline heap estimate is high",
                "The preflight render pipeline estimates more heap than a small MCU profile usually wants to spend on one app frame.",
                "Reduce DOM depth, large images, nested layout, extra layers and heavy visual effects before testing on a small internal-RAM target.",
                name, {"estimatedHeapBytes": heap})
        if framebuffer > 220160:
            append_performance_advice(
                advice, seen, "performance-full-frame-present-bytes", "info",
                "Full-frame present is expensive on this target",
                "The first paint uses a full-frame repaint/present. On RGB565 panels this is often dominated by conversion, DMA and panel flush time.",
                "Keep scrolling and animations on dirty rectangles or scroll-strip paths; use port telemetry to confirm converted pixels, packed bytes and DMA wait.",
                name, {"framebufferBytes": framebuffer})
        frame_update = target.get("frameUpdate", {}) if isinstance(target.get("frameUpdate", {}), dict) else {}
        if frame_update.get("repaint") == "full-frame" and frame_update.get("reason") != "first-paint":
            append_performance_advice(
                advice, seen, "performance-unexpected-full-frame-repaint", "warning",
                "A target is repainting the full frame after first paint",
                "Full-frame repaint outside initial load usually hides dirty-rect wins and can make scrolling or animation feel delayed.",
                "Look for layout-changing animation, viewport-sized invalidation, framebuffer mismatch, or host events that mark the whole tree dirty.",
                name, {"reason": str(frame_update.get("reason", "")), "action": str(frame_update.get("action", ""))})
        if commands > 96:
            append_performance_advice(
                advice, seen, "performance-display-command-count", "warning",
                "Display command count is high",
                "Many display commands increase layer flattening and dirty-rect replay work on CPU-rendered embedded targets.",
                "Merge decorative boxes, prefer CSS backgrounds over extra DOM where possible, and avoid rebuilding static decoration every frame.",
                name, {"displayCommands": commands})
        if layers > 16:
            append_performance_advice(
                advice, seen, "performance-layer-count", "warning",
                "Layer count is high",
                "Many layers make hit testing, sorting, compositing and dirty-region analysis more expensive.",
                "Use positioning and transforms only where they buy real behavior; keep repeated decorative elements in normal flow when possible.",
                name, {"layers": layers})

    resource_percent = int(summary.get("resourceBudgetPercent", 0) or 0)
    if resource_percent >= 80:
        append_performance_advice(
            advice, seen, "performance-resource-budget-high", "warning",
            "Package resources are close to the budget",
            "Large bundled resources increase install size, flash pressure and image/font decode pressure even before runtime work starts.",
            "Compress or subset assets, remove unused images/audio/fonts, and prefer generated CSS/canvas decoration only when it is cheaper on the target.",
            "", {"resourceBudgetPercent": resource_percent})
    display_percent = int(summary.get("displayCommandBudgetPercent", 0) or 0)
    if display_percent >= 80:
        append_performance_advice(
            advice, seen, "performance-display-command-budget-high", "warning",
            "Display commands are close to the manifest budget",
            "The page is near the configured display-command cap; future UI states may overflow or trigger degraded rendering.",
            "Raise the budget only after measuring the target, or simplify repeated decoration and offscreen content.",
            "", {"displayCommandBudgetPercent": display_percent})

    runtime_metrics = report.get("runtimeMetrics", {}) if isinstance(report.get("runtimeMetrics", {}), dict) else {}
    runtime_summary = runtime_metrics.get("summary", {}) if isinstance(runtime_metrics.get("summary", {}), dict) else {}
    if int(runtime_summary.get("loadOverloaded", 0) or 0) > 0:
        append_performance_advice(
            advice, seen, "performance-runtime-overloaded", "warning",
            "Runtime capture reported overloaded frames",
            "The Win32 frame-script log shows overloaded frames, which means the page or host work could not stay within the selected frame budget.",
            "Reduce display-command density, cut expensive effects, or move slow work into host jobs until the overloaded counter stops increasing.",
            "", {"loadOverloadedFrames": int(runtime_summary.get("loadOverloaded", 0) or 0)})
    if int(runtime_summary.get("loadDropAnimation", 0) or 0) > 0:
        append_performance_advice(
            advice, seen, "performance-runtime-drop-animation", "warning",
            "Runtime capture dropped animation frames",
            "The frame-script log shows animation drops, which usually means the target cannot afford the current animation load every frame.",
            "Lower animation frequency, simplify transforms/easing, or keep the animated region smaller so the dirty-rect path can stay cheap.",
            "", {"dropAnimationFrames": int(runtime_summary.get("loadDropAnimation", 0) or 0)})
    if int(runtime_summary.get("maxDirtyPercent", 0) or 0) >= 90:
        append_performance_advice(
            advice, seen, "performance-runtime-dirty-area-high", "info",
            "Runtime capture shows a large dirty area",
            "The runtime log reports that the dirty area regularly approaches the full viewport.",
            "Check whether layout changes or large repaints are forcing full-screen work; try to keep scrolling and animations on smaller dirty rectangles.",
            "", {"maxDirtyPercent": int(runtime_summary.get("maxDirtyPercent", 0) or 0)})
    scroll_copied_pixels = int(runtime_summary.get("scrollCopiedPixels", 0) or 0)
    converted_pixels = int(runtime_summary.get("convertedPixels", 0) or 0)
    if scroll_copied_pixels > 0 and converted_pixels > 0:
        append_performance_advice(
            advice, seen, "performance-runtime-scroll-strip-active", "info",
            "Runtime capture used the scroll-strip path",
            "The Win32 capture reports scroll-strip copies, so scroll work is staying on the cheaper incremental path instead of repainting only from scratch.",
            "Keep fixed headers/nav outside the scroll area and watch converted pixels; if converted pixels still grows toward full-screen cost, inspect dirty area diagnostics.",
            "", {"scrollCopiedPixels": scroll_copied_pixels, "convertedPixels": converted_pixels})
    port_telemetry = report.get("portTelemetry", {}) if isinstance(report.get("portTelemetry", {}), dict) else {}
    port_summary = port_telemetry.get("summary", {}) if isinstance(port_telemetry.get("summary", {}), dict) else {}
    average_frame_ms = float(port_summary.get("averageFrameMs", 0) or 0)
    max_frame_ms = float(port_summary.get("maxFrameMs", 0) or 0)
    if average_frame_ms > 33.4 or max_frame_ms > 50.0:
        append_performance_advice(
            advice, seen, "performance-port-frame-time-high", "warning",
            "Real-device frame time is above the 30 FPS budget",
            "The port telemetry reports measured frame time that can make touch scrolling or animation feel delayed on a wearable screen.",
            "Compare DMA wait, flush-done time and dirty area. Prefer dirty rectangles or scroll-strip paths before adding retained-rendering complexity.",
            "", {"averageFrameMs": average_frame_ms, "maxFrameMs": max_frame_ms})
    average_dma_wait_ms = float(port_summary.get("averageDmaWaitMs", 0) or 0)
    max_dma_wait_ms = float(port_summary.get("maxDmaWaitMs", 0) or 0)
    if average_dma_wait_ms > 4.0 or max_dma_wait_ms > 10.0:
        append_performance_advice(
            advice, seen, "performance-port-dma-wait-high", "info",
            "Panel DMA wait is a visible part of frame time",
            "The real port spends measurable time waiting for panel/DMA completion after JellyFrame has prepared pixels.",
            "Make sure the host waits for flush completion at the frame boundary, uses internal RAM only for DMA-visible strips, and avoids full-frame flushes during scroll.",
            "", {"averageDmaWaitMs": average_dma_wait_ms, "maxDmaWaitMs": max_dma_wait_ms})
    average_flush_done_ms = float(port_summary.get("averageFlushDoneMs", 0) or 0)
    max_flush_done_ms = float(port_summary.get("maxFlushDoneMs", 0) or 0)
    if average_flush_done_ms > 12.0 or max_flush_done_ms > 25.0:
        append_performance_advice(
            advice, seen, "performance-port-flush-done-high", "warning",
            "Panel flush completion is expensive",
            "The real port reports high flush-done time, so the panel transfer path may dominate the user-visible frame.",
            "Check bus speed, flush rectangle count, RGB565 packing, strip height and whether the port accidentally copies full frames.",
            "", {"averageFlushDoneMs": average_flush_done_ms, "maxFlushDoneMs": max_flush_done_ms})
    internal_ram_peak = int(port_summary.get("internalRamPeakBytes", 0) or 0)
    if internal_ram_peak > 256 * 1024:
        append_performance_advice(
            advice, seen, "performance-port-internal-ram-high", "warning",
            "Real port reports high internal RAM pressure",
            "The port telemetry says the frame path uses a large amount of internal RAM, which can starve RTOS stacks, DMA buffers and host services.",
            "Keep full framebuffers and decoded resources in PSRAM when possible; reserve internal RAM for DMA-visible strips, small scratch buffers and RTOS-critical work.",
            "", {"internalRamPeakBytes": internal_ram_peak})
    return advice


def append_developer_advice(advice: list[dict],
                            seen: set[tuple[str, str, str]],
                            code: str,
                            severity: str,
                            source: str = "",
                            detail: str = "",
                            target: str = "") -> None:
    template = advice_template_for_code(code)
    if not template:
        return
    key = (code, source or target, detail[:120])
    if key in seen:
        return
    seen.add(key)
    entry = {
        "code": code,
        "severity": severity,
        "title": template["title"],
        "explanation": template["explanation"],
        "action": template["action"],
    }
    if "recipe" in template:
        entry["recipe"] = template["recipe"]
    if source:
        entry["source"] = source
    if detail:
        entry["detail"] = detail
    if target:
        entry["target"] = target
    parsed_detail = parse_diagnostic_detail(detail)
    if parsed_detail:
        if "text" in parsed_detail:
            entry["text"] = parsed_detail["text"]
        if "node" in parsed_detail:
            entry["node"] = parsed_detail["node"]
        if "path" in parsed_detail:
            entry["path"] = parsed_detail["path"]
        if "viewport" in parsed_detail:
            entry["viewport"] = parsed_detail["viewport"]
        if "paintBounds" in parsed_detail:
            entry["paintBounds"] = parsed_detail["paintBounds"]
        metrics = diagnostic_metrics_from_detail(parsed_detail)
        if metrics:
            entry["metrics"] = metrics
        specialize_developer_advice(entry, code, parsed_detail)
    advice.append(entry)


def collect_developer_advice(report: dict) -> list[dict]:
    advice: list[dict] = []
    seen: set[tuple[str, str, str]] = set()

    for warning in report.get("warnings", []):
        if not isinstance(warning, dict):
            continue
        append_developer_advice(advice,
                                seen,
                                str(warning.get("code", "")),
                                str(warning.get("level", "warning")),
                                str(warning.get("source", "")),
                                str(warning.get("message", "")),
                                str(warning.get("target", "")))

    pipeline = report.get("pipelineDiagnostics", {})
    if isinstance(pipeline, dict):
        for diagnostic in pipeline.get("diagnostics", []):
            if not isinstance(diagnostic, dict):
                continue
            append_developer_advice(advice,
                                    seen,
                                    str(diagnostic.get("code", "")),
                                    str(diagnostic.get("severity", "warning")),
                                    str(diagnostic.get("stage", "")),
                                    str(diagnostic.get("detail", "")))

    for profile in report.get("responsiveProfiles", []):
        if not isinstance(profile, dict):
            continue
        target = str(profile.get("target", ""))
        status = str(profile.get("status", ""))
        layout = profile.get("layout", {}) if isinstance(profile.get("layout", {}), dict) else {}
        sample_codes = {
            str(diagnostic.get("code", ""))
            for diagnostic in profile.get("diagnosticSamples", [])
            if isinstance(diagnostic, dict)
        }
        if status == "horizontal-overflow" or bool(layout.get("horizontalOverflow", False)):
            if "visual-horizontal-overflow" not in sample_codes:
                append_developer_advice(advice,
                                        seen,
                                        "visual-horizontal-overflow",
                                        "warning",
                                        "",
                                        "Responsive profile reports horizontal overflow.",
                                        target)
        if status == "scroll-needed" or bool(layout.get("verticalOverflow", False)):
            if "visual-scroll-needed" not in sample_codes:
                append_developer_advice(advice,
                                        seen,
                                        "visual-scroll-needed",
                                        "info",
                                        "",
                                        "Responsive profile reports content taller than the viewport.",
                                        target)
        gate = profile.get("gate", {})
        if isinstance(gate, dict) and gate.get("decision") in {"warn", "reject"}:
            append_developer_advice(advice,
                                    seen,
                                    "target-gate-not-accepted",
                                    "error" if gate.get("decision") == "reject" else "warning",
                                    "",
                                    "Target gate reasons: " + ", ".join(gate.get("reasons", [])),
                                    target)
        for diagnostic in profile.get("diagnosticSamples", []):
            if not isinstance(diagnostic, dict):
                continue
            append_developer_advice(advice,
                                    seen,
                                    str(diagnostic.get("code", "")),
                                    str(diagnostic.get("severity", "warning")),
                                    str(diagnostic.get("stage", "")),
                                    str(diagnostic.get("detail", "")),
                                    target)

    font_diagnostics = report.get("fontDiagnostics", {})
    if isinstance(font_diagnostics, dict):
        if int(font_diagnostics.get("missingNonAsciiCodepointCount", 0) or 0) > 0:
            append_developer_advice(advice,
                                    seen,
                                    "font-missing-glyphs",
                                    "warning",
                                    "jellyframe.app.json",
                                    "Target/app fonts miss non-ASCII source codepoints.")
        usage = font_diagnostics.get("fontFamilyUsage", {})
        if isinstance(usage, dict) and int(usage.get("unmatchedPrimaryCount", 0) or 0) > 0:
            append_developer_advice(advice,
                                    seen,
                                    "font-family-unmatched",
                                    "warning",
                                    "styles",
                                    "One or more CSS primary font families do not match manifest fonts.")

    return advice


def enrich_report(report: dict) -> dict:
    report.pop("developerAdvice", None)
    report.pop("performanceSummary", None)
    report.pop("performanceAdvice", None)
    performance_summary = collect_performance_summary(report)
    report["performanceSummary"] = performance_summary
    performance_advice = collect_performance_advice(report, performance_summary)
    if performance_advice:
        report["performanceAdvice"] = performance_advice
    advice = collect_developer_advice(report)
    if advice:
        report["developerAdvice"] = advice
    return report


def write_json_report(path: Path, report: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    enrich_report(report)
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def merge_pipeline_report(package_report_path: Path, pipeline_report: dict) -> None:
    if not pipeline_report:
        return
    report = load_json_if_exists(package_report_path)
    if not report:
        report = {
            "format": "jellyframe.package.report",
        }
    report["pipelineDiagnostics"] = pipeline_report
    write_json_report(package_report_path, report)


def merge_responsive_profiles(package_report_path: Path, profiles: list[dict]) -> None:
    if not profiles:
        return
    report = load_json_if_exists(package_report_path)
    if not report:
        report = {
            "format": "jellyframe.package.report",
        }
    report["responsiveProfiles"] = profiles
    write_json_report(package_report_path, report)


def merge_font_subset_report(package_report_path: Path, font_subset: dict) -> None:
    if not font_subset:
        return
    report = load_json_if_exists(package_report_path)
    if not report:
        report = {
            "format": "jellyframe.package.report",
        }
    report["fontSubset"] = font_subset
    write_json_report(package_report_path, report)


def diagnostic_samples(report: dict, limit: int = 12) -> list[dict]:
    diagnostics = report.get("diagnostics", []) if isinstance(report, dict) else []
    samples = []

    def diagnostic_priority(diagnostic: dict) -> tuple[int, int]:
        code = str(diagnostic.get("code", ""))
        severity = str(diagnostic.get("severity", "info"))
        if code in ADVICE_IGNORED_CODES:
            return (3, 0)
        severity_rank = {
            "error": 0,
            "warning": 1,
            "info": 2,
        }.get(severity, 2)
        advice_rank = 0 if advice_template_for_code(code) else 1
        return (severity_rank, advice_rank)

    actionable = [
        diagnostic for diagnostic in diagnostics
        if isinstance(diagnostic, dict) and str(diagnostic.get("code", "")) not in ADVICE_IGNORED_CODES
    ]
    actionable.sort(key=diagnostic_priority)
    for diagnostic in actionable:
        if not isinstance(diagnostic, dict):
            continue
        sample = {
            "stage": str(diagnostic.get("stage", "")),
            "severity": str(diagnostic.get("severity", "info")),
            "code": str(diagnostic.get("code", "")),
        }
        detail = str(diagnostic.get("detail", ""))
        if detail:
            sample["detail"] = detail
            parsed_detail = parse_diagnostic_detail(detail)
            if parsed_detail:
                if "text" in parsed_detail:
                    sample["text"] = parsed_detail["text"]
                if "node" in parsed_detail:
                    sample["node"] = parsed_detail["node"]
                if "path" in parsed_detail:
                    sample["path"] = parsed_detail["path"]
                if "viewport" in parsed_detail:
                    sample["viewport"] = parsed_detail["viewport"]
                if "paintBounds" in parsed_detail:
                    sample["paintBounds"] = parsed_detail["paintBounds"]
                metrics = diagnostic_metrics_from_detail(parsed_detail)
                if metrics:
                    sample["metrics"] = metrics
        samples.append(sample)
        if len(samples) >= limit:
            break
    return samples


def remember_pipeline_report(args: argparse.Namespace, pipeline_report_path: Path) -> None:
    args._pipeline_report = load_json_if_exists(pipeline_report_path)
    merge_pipeline_report(args.report, args._pipeline_report)


def diagnostic_counts(report: dict) -> tuple[int, int, int]:
    summary = report.get("summary", {}) if isinstance(report, dict) else {}
    errors = int(summary.get("error", 0) or 0)
    warnings = int(summary.get("warning", 0) or 0)
    infos = int(summary.get("info", 0) or 0)
    return errors, warnings, infos


def responsive_status(pipeline_report: dict) -> str:
    errors, warnings, _ = diagnostic_counts(pipeline_report)
    layout = pipeline_report.get("layout", {}) if isinstance(pipeline_report, dict) else {}
    pipeline = pipeline_report.get("pipeline", {}) if isinstance(pipeline_report, dict) else {}
    if errors > 0:
        return "diagnostics-error"
    if bool(layout.get("horizontalOverflow", False)):
        return "horizontal-overflow"
    if warnings > 0:
        return "diagnostics-warning"
    if bool(layout.get("verticalOverflow", False)):
        return "scroll-needed"
    if int(pipeline.get("framebufferBytes", 0) or 0) <= 0:
        return "budget-warning"
    return "fits"


def load_manifest(root: Path) -> dict:
    manifest_path = root / "jellyframe.app.json"
    if not manifest_path.is_file():
        return {}
    return json.loads(manifest_path.read_text(encoding="utf-8-sig"))


def target_gate_config(root: Path, target: str) -> dict:
    manifest = load_manifest(root)
    targets = manifest.get("targets", {})
    if not isinstance(targets, dict):
        return {}
    target_entry = targets.get(target, {})
    if not isinstance(target_entry, dict):
        return {}
    gate = target_entry.get("gate", {})
    return gate if isinstance(gate, dict) else {}


def responsive_gate_for_profile(profile: dict, gate: dict) -> dict:
    if not gate:
        return {
            "policy": "none",
            "decision": "not-declared",
            "reasons": [],
        }
    policy = gate.get("policy", "warn")
    if policy not in {"accept", "warn", "reject"}:
        policy = "warn"
    allow_scroll = bool(gate.get("allowScroll", True))
    allow_horizontal_overflow = bool(gate.get("allowHorizontalOverflow", False))
    max_warnings = gate.get("maxWarnings", None)
    max_errors = gate.get("maxErrors", 0)
    min_viewport = gate.get("minViewport", {})
    if not isinstance(min_viewport, dict):
        min_viewport = {}

    reasons = []
    viewport = profile.get("viewport", {}) if isinstance(profile.get("viewport", {}), dict) else {}
    layout = profile.get("layout", {}) if isinstance(profile.get("layout", {}), dict) else {}
    diagnostics = profile.get("diagnostics", {}) if isinstance(profile.get("diagnostics", {}), dict) else {}
    min_width = int(min_viewport.get("width", 0) or 0)
    min_height = int(min_viewport.get("height", 0) or 0)
    if min_width and int(viewport.get("width", 0) or 0) < min_width:
        reasons.append(f"viewport-width<{min_width}")
    if min_height and int(viewport.get("height", 0) or 0) < min_height:
        reasons.append(f"viewport-height<{min_height}")
    if bool(layout.get("horizontalOverflow", False)) and not allow_horizontal_overflow:
        reasons.append("horizontal-overflow")
    if profile.get("status") == "scroll-needed" and not allow_scroll:
        reasons.append("scroll-needed")
    warning_count = int(diagnostics.get("warning", 0) or 0)
    error_count = int(diagnostics.get("error", 0) or 0)
    if isinstance(max_warnings, int) and warning_count > max_warnings:
        reasons.append(f"warnings>{max_warnings}")
    if isinstance(max_errors, int) and error_count > max_errors:
        reasons.append(f"errors>{max_errors}")

    decision = "accept" if not reasons else policy
    return {
        "policy": policy,
        "decision": decision,
        "reasons": reasons,
        "allowScroll": allow_scroll,
        "allowHorizontalOverflow": allow_horizontal_overflow,
        "maxWarnings": max_warnings if isinstance(max_warnings, int) else None,
        "maxErrors": max_errors if isinstance(max_errors, int) else None,
        "minViewport": {
            "width": min_width,
            "height": min_height,
        },
    }


def responsive_profile_from_pipeline(target: str, target_config: dict, pipeline_report: dict) -> dict:
    viewport = pipeline_report.get("viewport", {}) if isinstance(pipeline_report, dict) else {}
    layout = pipeline_report.get("layout", {}) if isinstance(pipeline_report, dict) else {}
    pipeline = pipeline_report.get("pipeline", {}) if isinstance(pipeline_report, dict) else {}
    summary = pipeline_report.get("summary", {}) if isinstance(pipeline_report, dict) else {}
    frame_update = pipeline_report.get("frameUpdate", {}) if isinstance(pipeline_report, dict) else {}
    timings = pipeline_report.get("timingsUs", {}) if isinstance(pipeline_report, dict) else {}
    target_viewport = target_config.get("viewport", {}) if isinstance(target_config.get("viewport", {}), dict) else {}
    shape = target_viewport.get("shape", "")
    return {
        "target": target,
        "status": responsive_status(pipeline_report),
        "viewport": {
            "width": int(viewport.get("width", 0) or 0),
            "height": int(viewport.get("height", 0) or 0),
            "shape": shape if isinstance(shape, str) else "",
        },
        "layout": {
            "contentHeight": int(layout.get("contentHeight", viewport.get("height", 0)) or 0),
            "horizontalOverflow": bool(layout.get("horizontalOverflow", False)),
            "verticalOverflow": bool(layout.get("verticalOverflow", False)),
            "bounds": layout.get("bounds", {}) if isinstance(layout.get("bounds", {}), dict) else {},
            "paintBounds": layout.get("paintBounds", {}) if isinstance(layout.get("paintBounds", {}), dict) else {},
        },
        "pipeline": {
            "domNodes": int(pipeline.get("domNodes", 0) or 0),
            "renderObjects": int(pipeline.get("renderObjects", 0) or 0),
            "layoutBoxes": int(pipeline.get("layoutBoxes", 0) or 0),
            "layers": int(pipeline.get("layers", 0) or 0),
            "displayCommands": int(pipeline.get("displayCommands", 0) or 0),
            "framebufferBytes": int(pipeline.get("framebufferBytes", 0) or 0),
            "estimatedHeapBytes": int(pipeline.get("estimatedHeapBytes", 0) or 0),
        },
        "frameUpdate": frame_update if isinstance(frame_update, dict) else {},
        "timingsUs": timings if isinstance(timings, dict) else {},
        "diagnostics": {
            "total": int(summary.get("total", 0) or 0),
            "info": int(summary.get("info", 0) or 0),
            "warning": int(summary.get("warning", 0) or 0),
            "error": int(summary.get("error", 0) or 0),
        },
        "diagnosticSamples": diagnostic_samples(pipeline_report),
    }


def diagnostic_status_from_report(package_report_path: Path) -> tuple[int, int, int]:
    report = load_json_if_exists(package_report_path)
    pipeline = report.get("pipelineDiagnostics", {})
    errors, warnings, infos = diagnostic_counts(pipeline)
    package_warnings = report.get("warnings", [])
    if isinstance(package_warnings, list):
        warnings += len(package_warnings)
    responsive_profiles = report.get("responsiveProfiles", [])
    if isinstance(responsive_profiles, list):
        for profile in responsive_profiles:
            if not isinstance(profile, dict):
                continue
            diagnostics = profile.get("diagnostics", {})
            if isinstance(diagnostics, dict):
                errors += int(diagnostics.get("error", 0) or 0)
                warnings += int(diagnostics.get("warning", 0) or 0)
                infos += int(diagnostics.get("info", 0) or 0)
            gate = profile.get("gate", {})
            if isinstance(gate, dict):
                decision = gate.get("decision", "")
                if decision == "reject":
                    errors += 1
                elif decision == "warn":
                    warnings += 1
    return errors, warnings, infos


def enforce_diagnostics_policy(args: argparse.Namespace) -> int:
    errors, warnings, infos = diagnostic_status_from_report(args.report)
    print(f"diagnostic policy: errors={errors} warnings={warnings} info={infos}")
    if errors > 0:
        return 1
    if getattr(args, "strict", False) and warnings > 0:
        return 1
    return 0


def run_pipeline_check(args: argparse.Namespace, target_override: str | None = None) -> tuple[int, dict]:
    pseudo_browser = tool_path(args.build_dir, "jellyframe_pseudo_browser")
    ensure_tool(pseudo_browser)
    manifest_path = args.root / "jellyframe.app.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    entry = str(manifest.get("entry", "/index.html"))
    entry_path = args.root / Path(*entry.lstrip("/").split("/"))
    target = target_override if target_override is not None else getattr(args, "target", None)
    target_config = effective_target_config(args.root, target) if target else {}
    viewport = target_config.get("viewport", {}) if isinstance(target_config.get("viewport", {}), dict) else {}
    width = int(viewport.get("width", 0) or 0)
    height = int(viewport.get("height", 0) or 0)
    with tempfile.TemporaryDirectory(prefix="jellyframe-pipeline-check-") as directory:
        empty_css = Path(directory) / "empty.css"
        empty_css.write_text("", encoding="utf-8")
        output = Path(directory) / "preflight.bmp"
        diagnostics_json = Path(directory) / "pipeline.diagnostics.json"
        command = [
            str(pseudo_browser),
            str(entry_path),
            str(empty_css),
            str(output),
        ]
        if width:
            command.append(str(width))
        if height:
            command.append(str(height))
        command.extend(["--diagnostics-json", str(diagnostics_json)])
        result = run_command(command)
        if result == 0:
            return result, load_json_if_exists(diagnostics_json)
        return result, {}


def run_single_pipeline_check(args: argparse.Namespace) -> int:
    result, pipeline_report = run_pipeline_check(args)
    if result == 0:
        args._pipeline_report = pipeline_report
        merge_pipeline_report(args.report, pipeline_report)
    return result


def run_responsive_profile_checks(args: argparse.Namespace) -> int:
    targets = requested_targets(args)
    if not targets:
        return run_single_pipeline_check(args)
    profiles = []
    for target in targets:
        result, pipeline_report = run_pipeline_check(args, target)
        if result != 0:
            return result
        target_config = effective_target_config(args.root, target)
        profile = responsive_profile_from_pipeline(target, target_config, pipeline_report)
        profile["gate"] = responsive_gate_for_profile(profile, target_gate_config(args.root, target))
        profiles.append(profile)
        print(
            "responsive "
            f"{target}: {profile['status']} "
            f"gate={profile['gate']['decision']} "
            f"viewport={profile['viewport']['width']}x{profile['viewport']['height']} "
            f"content={profile['layout']['contentHeight']} "
            f"overflowX={profile['layout']['horizontalOverflow']} "
            f"diagnostics={profile['diagnostics']['error']}/"
            f"{profile['diagnostics']['warning']}/"
            f"{profile['diagnostics']['info']}"
        )
    args._responsive_profiles = profiles
    if profiles:
        merge_responsive_profiles(args.report, profiles)
    return 0


def run_package_preflight(args: argparse.Namespace, include_pipeline: bool) -> int:
    validate_result = cmd_validate(args)
    if validate_result != 0 or getattr(args, "skip_check", False):
        return validate_result
    if include_pipeline:
        pipeline_result = run_responsive_profile_checks(args)
        if pipeline_result != 0:
            return pipeline_result
    if should_run_font_resource_check(args):
        font_result = run_font_resource_check(args)
        if font_result != 0:
            return font_result
    return enforce_diagnostics_policy(args) if include_pipeline else 0


def cmd_package(args: argparse.Namespace) -> int:
    preflight_result = run_package_preflight(args, include_pipeline=True)
    if preflight_result != 0:
        return preflight_result
    package_result = run_command(package_command(args, False))
    if package_result != 0:
        return package_result
    merge_pipeline_report(args.report, getattr(args, "_pipeline_report", {}))
    merge_responsive_profiles(args.report, getattr(args, "_responsive_profiles", []))
    if getattr(args, "runtime_log", None):
        merge_runtime_capture_report(args.report, args.runtime_log)
    if getattr(args, "port_telemetry", None):
        merge_port_telemetry_report(args.report, args.port_telemetry)
    return enforce_diagnostics_policy(args)


def cmd_preview(args: argparse.Namespace) -> int:
    if args.report is None:
        args.report = args.output.with_suffix(".report.json")
    preflight_result = run_package_preflight(args, include_pipeline=True)
    if preflight_result != 0:
        return preflight_result
    win32_browser = tool_path(args.build_dir, "jellyframe_win32_browser")
    ensure_tool(win32_browser)
    target_config = effective_target_config(args.root, args.target) if args.target else {}
    viewport = target_config.get("viewport", {}) if isinstance(target_config.get("viewport", {}), dict) else {}
    width = args.width or int(viewport.get("width", 0) or 0)
    height = args.height or int(viewport.get("height", 0) or 0)
    with tempfile.TemporaryDirectory(prefix="jellyframe-preview-package-") as directory:
        previous_debug_dir = getattr(args, "debug_dir", None)
        args.debug_dir = Path(directory) / "package"
        package_result = run_command(package_command(args, False))
        args.debug_dir = previous_debug_dir
        if package_result != 0:
            return package_result
        merge_pipeline_report(args.report, getattr(args, "_pipeline_report", {}))
        merge_responsive_profiles(args.report, getattr(args, "_responsive_profiles", []))
        command = [
            str(win32_browser),
            "--capture",
            str(args.output),
            "--app",
            str(Path(directory) / "package"),
        ]
        if width:
            command.extend(["--viewport-width", str(width)])
        if height:
            command.extend(["--viewport-height", str(height)])
        result = run_command(command)
    if result == 0:
        if getattr(args, "runtime_log", None):
            merge_runtime_capture_report(args.report, args.runtime_log)
        if getattr(args, "port_telemetry", None):
            merge_port_telemetry_report(args.report, args.port_telemetry)
        return enforce_diagnostics_policy(args)
    return result


def resource_files_from_report(root: Path, report_path: Path) -> list[str]:
    report = json.loads(report_path.read_text(encoding="utf-8-sig"))
    files = []
    text_other_suffixes = {
        ".html",
        ".htm",
        ".json",
        ".md",
        ".svg",
        ".txt",
        ".xml",
        ".jfcapture",
    }
    for resource in report.get("resources", []):
        kind = resource.get("kind")
        if kind not in {"Stylesheet", "ClassicScript", "Other"}:
            continue
        resource_path = resource.get("path", "")
        if not resource_path:
            continue
        if kind == "Other" and Path(resource_path).suffix.lower() not in text_other_suffixes:
            continue
        relative = resource_path[1:] if resource_path.startswith("/") else resource_path
        candidate = root / Path(*relative.split("/"))
        if candidate.is_file():
            files.append(str(candidate))

    static_modules = report.get("staticModules", {})
    if isinstance(static_modules, dict) and static_modules.get("enabled"):
        for module in static_modules.get("modules", []):
            if not isinstance(module, dict):
                continue
            module_path = module.get("path", "")
            if not isinstance(module_path, str) or not module_path:
                continue
            relative = module_path[1:] if module_path.startswith("/") else module_path
            candidate = root / Path(*relative.split("/"))
            if candidate.is_file() and str(candidate) not in files:
                files.append(str(candidate))
    return files


def run_font_resource_check(args: argparse.Namespace) -> int:
    font_check = tool_path(args.build_dir, "jellyframe_font_resource_check")
    ensure_tool(font_check)
    files = resource_files_from_report(args.root, args.report)
    command = [str(font_check)]
    font_subset_mode = getattr(args, "font_subset", "auto") or "auto"
    if font_subset_mode not in {"auto", "off"}:
        emit_used_chars = Path(font_subset_mode)
    else:
        emit_used_chars = getattr(args, "emit_used_chars", None)
        if font_subset_mode == "auto" and emit_used_chars is None:
            emit_used_chars = args.report.with_suffix(".used_chars.txt")
    font_coverage = getattr(args, "font_coverage", None)
    if font_coverage:
        command.extend(["--font-coverage", str(font_coverage)])
    command.extend(["--font-budget", effective_font_budget(args)])
    if emit_used_chars:
        emit_used_chars.parent.mkdir(parents=True, exist_ok=True)
        command.extend(["--emit-used-chars", str(emit_used_chars)])
    command.extend(files)
    result = run_command(command)
    if result != 0:
        return result

    generated_font = getattr(args, "font_output", None)
    font_source_bdf = getattr(args, "font_source_bdf", None)
    generated = False
    if generated_font and font_subset_mode == "off":
        raise SystemExit("--font-output requires --font-subset auto or a used-chars path")
    if generated_font and not font_source_bdf:
        raise SystemExit("--font-output requires --font-source-bdf")
    if font_source_bdf:
        if font_subset_mode == "off":
            raise SystemExit("--font-source-bdf requires --font-subset auto or a used-chars path")
        if not generated_font:
            raise SystemExit("--font-source-bdf requires --font-output")
        if not emit_used_chars:
            raise SystemExit("font subset generation requires a used-chars output path")
        font_pack_gen = tool_path(args.build_dir, "jellyframe_font_pack_gen")
        ensure_tool(font_pack_gen)
        generated_font.parent.mkdir(parents=True, exist_ok=True)
        font_command = [
            str(font_pack_gen),
            "--bdf",
            str(font_source_bdf),
            "--chars",
            str(emit_used_chars),
            "--output-binary",
            str(generated_font),
            "--coverage-bits",
            str(getattr(args, "font_coverage_bits", 1)),
        ]
        if getattr(args, "font_allow_missing", False):
            font_command.append("--allow-missing")
        result = run_command(font_command)
        if result != 0:
            return result
        generated = True

    merge_font_subset_report(args.report, {
        "mode": font_subset_mode,
        "usedChars": str(emit_used_chars) if emit_used_chars else "",
        "sourceBdf": str(font_source_bdf) if font_source_bdf else "",
        "generatedFont": str(generated_font) if generated_font else "",
        "generated": generated,
        "coverageBits": int(getattr(args, "font_coverage_bits", 1)),
        "note": (
            "Generated .jffont files must still be declared in manifest fonts[] before runtime use."
            if generated else
            "Use --font-source-bdf and --font-output to generate a .jffont supplement from the scanned used chars."
        ),
    })
    return 0


def cmd_check(args: argparse.Namespace) -> int:
    validate_result = cmd_validate(args)
    if validate_result != 0:
        return validate_result
    if not getattr(args, "skip_check", False):
        pipeline_result = run_responsive_profile_checks(args)
        if pipeline_result != 0:
            return pipeline_result
    if should_run_font_resource_check(args):
        font_result = run_font_resource_check(args)
        if font_result != 0:
            return font_result
    if getattr(args, "runtime_log", None):
        merge_runtime_capture_report(args.report, args.runtime_log)
    if getattr(args, "port_telemetry", None):
        merge_port_telemetry_report(args.report, args.port_telemetry)
    policy_result = enforce_diagnostics_policy(args)
    if policy_result != 0:
        return policy_result
    if getattr(args, "skip_check", False):
        print("package is valid; developer preflight checks were skipped by request.")
    else:
        print("package is valid; pipeline diagnostics ran through the render-core pseudo browser.")
        if getattr(args, "no_font_check", False):
            print("font resource preflight was skipped by request.")
        else:
            print("font resource preflight completed.")
    return 0


def cmd_font(args: argparse.Namespace) -> int:
    validate_result = cmd_validate(args)
    if validate_result != 0:
        return validate_result
    args.used_chars.parent.mkdir(parents=True, exist_ok=True)
    args.emit_used_chars = args.used_chars
    check_result = run_font_resource_check(args)
    if check_result != 0 or not args.bdf:
        return check_result
    if not args.output_header and not args.output_binary:
        raise SystemExit("--output-header or --output-binary is required when --bdf is provided")
    font_pack_gen = tool_path(args.build_dir, "jellyframe_font_pack_gen")
    ensure_tool(font_pack_gen)
    font_command = [
        str(font_pack_gen),
        "--bdf",
        str(args.bdf),
        "--chars",
        str(args.used_chars),
        "--name",
        args.name,
    ]
    if args.output_header:
        args.output_header.parent.mkdir(parents=True, exist_ok=True)
        font_command.extend(["--output", str(args.output_header)])
    if args.output_binary:
        args.output_binary.parent.mkdir(parents=True, exist_ok=True)
        font_command.extend(["--output-binary", str(args.output_binary)])
    font_command.extend(["--coverage-bits", str(args.coverage_bits)])
    if args.allow_missing:
        font_command.append("--allow-missing")
    return run_command(font_command)


def cmd_schema(args: argparse.Namespace) -> int:
    path = schema_path()
    if args.print_path:
        print(path)
    else:
        print(path.read_text(encoding="utf-8"), end="")
    return 0


def cmd_targets(args: argparse.Namespace) -> int:
    presets = list_target_presets()
    if args.json:
        print(json.dumps(presets, ensure_ascii=False, indent=2))
        return 0
    for preset in presets:
        viewport = preset.get("viewport", {})
        viewport_text = f"{viewport.get('width', '?')}x{viewport.get('height', '?')} {viewport.get('shape', '')}".strip()
        print(f"{preset.get('id', '(unknown)')}: {viewport_text} - {preset.get('description', '')}")
    return 0


def update_template_manifest(manifest_path: Path, args: argparse.Namespace) -> None:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    if args.app_id:
        validate_app_id(args.app_id)
        manifest["id"] = args.app_id
    if args.name:
        manifest["name"] = args.name
    if args.target:
        target_config = load_target_config(args.target)
        if not target_config:
            raise SystemExit(f"unknown target preset: {args.target}")
        viewport = target_config.get("viewport", {})
        if isinstance(viewport, dict):
            width = int(viewport.get("width", 0) or 0)
            height = int(viewport.get("height", 0) or 0)
            shape = viewport.get("shape", "rect")
            manifest["viewport"] = {
                "designWidth": width,
                "designHeight": height,
                "shape": shape,
            }
            target_entry = {
                "viewport": viewport,
                "fontProfile": target_config.get("fontProfile", "tiny-plus-symbols"),
                "output": target_config.get("output", "cpp"),
            }
            for key in ("budgets", "framebuffer"):
                if isinstance(target_config.get(key), dict):
                    target_entry[key] = target_config[key]
            manifest["targets"] = {
                args.target: target_entry
            }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def cmd_templates(args: argparse.Namespace) -> int:
    templates = list_app_templates()
    if args.json:
        print(json.dumps({"templates": templates}, ensure_ascii=False, indent=2))
    else:
        for name in templates:
            print(name)
    return 0


def cmd_new(args: argparse.Namespace) -> int:
    template_path = app_templates_dir() / args.template
    if not template_path.is_dir():
        available = ", ".join(list_app_templates()) or "<none>"
        raise SystemExit(f"unknown template: {args.template}; available: {available}")
    if args.output.exists() and not args.output.is_dir():
        raise SystemExit(f"output path is not a directory: {args.output}")
    if args.output.exists() and any(args.output.iterdir()):
        raise SystemExit(f"output directory is not empty: {args.output}")
    args.output.mkdir(parents=True, exist_ok=True)
    shutil.copytree(template_path, args.output, dirs_exist_ok=True)
    update_template_manifest(args.output / "jellyframe.app.json", args)
    print(f"created {args.template} app at {args.output}")
    return 0


def cmd_registry(args: argparse.Namespace) -> int:
    registry_args = list(args.registry_args)
    if registry_args and registry_args[0] == "--":
        registry_args = registry_args[1:]
    return app_registry.main(registry_args)


def write_install_transaction_report(report_path: Path, transaction: dict, merge: bool) -> None:
    if merge and report_path.is_file():
        report = json.loads(report_path.read_text(encoding="utf-8-sig"))
        report["installTransaction"] = transaction
        write_json_report(report_path, report)
        return
    write_json_report(report_path, transaction)


def sample_package_roots(samples_dir: Path) -> list[Path]:
    if not samples_dir.is_dir():
        raise SystemExit(f"samples directory does not exist: {samples_dir}")
    roots = []
    for path in sorted(samples_dir.iterdir()):
        if path.is_dir() and (path / "jellyframe.app.json").is_file():
            roots.append(path)
    return roots


def parse_sample_name_filters(values: list[str] | None) -> list[str]:
    names: list[str] = []
    for value in values or []:
        for item in str(value).split(","):
            name = item.strip()
            if name and name not in names:
                names.append(name)
    return names


def filter_sample_roots(roots: list[Path],
                        include_values: list[str] | None,
                        exclude_values: list[str] | None) -> list[Path]:
    include_names = parse_sample_name_filters(include_values)
    exclude_names = set(parse_sample_name_filters(exclude_values))
    by_name = {root.name: root for root in roots}
    if include_names:
        missing = [name for name in include_names if name not in by_name]
        if missing:
            raise SystemExit("unknown sample package(s): " + ", ".join(missing))
        selected = [by_name[name] for name in include_names if name not in exclude_names]
    else:
        selected = [root for root in roots if root.name not in exclude_names]
    if not selected:
        raise SystemExit("no sample packages selected")
    return selected


def doctor_summary_from_report(sample: str, status: str, report_path: Path) -> dict:
    errors, warnings, infos = diagnostic_status_from_report(report_path)
    report = load_json_if_exists(report_path)
    performance = report.get("performanceSummary", {}) if isinstance(report.get("performanceSummary", {}), dict) else {}
    bottlenecks = []
    if int(performance.get("score", 0) or 0) >= 3:
        raw_bottlenecks = performance.get("bottlenecks", [])
        if not isinstance(raw_bottlenecks, list):
            raw_bottlenecks = []
        for item in raw_bottlenecks:
            if not isinstance(item, dict):
                continue
            code = str(item.get("code", ""))
            if code:
                bottlenecks.append(code)
            if len(bottlenecks) >= 3:
                break
    targets = []
    for profile in report.get("responsiveProfiles", []):
        if not isinstance(profile, dict):
            continue
        gate = profile.get("gate", {})
        gate_decision = gate.get("decision", "not-declared") if isinstance(gate, dict) else "not-declared"
        targets.append({
            "target": str(profile.get("target", "<target>")),
            "status": str(profile.get("status", "unknown")),
            "gate": str(gate_decision),
        })
    return {
        "sample": sample,
        "status": status,
        "errors": errors,
        "warnings": warnings,
        "infos": infos,
        "performance": str(performance.get("rating", "unknown")),
        "performanceScore": int(performance.get("score", 0) or 0),
        "bottlenecks": bottlenecks,
        "targets": targets,
        "report": str(report_path),
    }


def format_doctor_summary(row: dict) -> str:
    target_text = "-"
    targets = row.get("targets", [])
    if isinstance(targets, list) and targets:
        target_text = ", ".join(
            f"{target.get('target', '<target>')}:"
            f"{target.get('status', 'unknown')}/"
            f"{target.get('gate', 'not-declared')}"
            for target in targets
            if isinstance(target, dict)
        ) or "-"
    return (
        f"  {row.get('sample', '<sample>')}: {row.get('status', 'unknown')} "
        f"diagnostics={int(row.get('errors', 0) or 0)}/"
        f"{int(row.get('warnings', 0) or 0)}/"
        f"{int(row.get('infos', 0) or 0)} "
        f"perf={row.get('performance', 'unknown')}/{int(row.get('performanceScore', 0) or 0)} "
        f"bottlenecks={','.join(row.get('bottlenecks', []) or []) or '-'} "
        f"targets={target_text} report={row.get('report', '')}"
    )


def cmd_doctor(args: argparse.Namespace) -> int:
    ensure_tool(tool_path(args.build_dir, "jellyframe_pseudo_browser"))
    roots = sample_package_roots(args.samples_dir)
    if not roots:
        raise SystemExit(f"no sample packages found in {args.samples_dir}")
    roots = filter_sample_roots(roots,
                                getattr(args, "sample", None),
                                getattr(args, "exclude_sample", None))
    args.report_dir.mkdir(parents=True, exist_ok=True)
    print(
        "JellyFrame doctor: "
        f"samples={len(roots)} targets={args.targets or '<default>'} "
        f"build_dir={args.build_dir}"
    )
    failed = 0
    summaries = []
    for root in roots:
        report = args.report_dir / f"{root.name}.report.json"
        command = [
            sys.executable,
            str(Path(__file__).resolve()),
            "check",
            "--root",
            str(root),
            "--report",
            str(report),
            "--build-dir",
            str(args.build_dir),
        ]
        if args.targets:
            command.extend(["--targets", args.targets])
        if args.target:
            command.extend(["--target", args.target])
        if args.no_font_check:
            command.append("--no-font-check")
        if args.strict:
            command.append("--strict")
        result = run_command(command)
        status = "ok" if result == 0 else "failed"
        print(f"doctor sample {root.name}: {status} report={report}")
        summaries.append(doctor_summary_from_report(root.name, status, report))
        if result != 0:
            failed += 1
            if args.fail_fast:
                break
    total_errors = sum(int(row.get("errors", 0) or 0) for row in summaries)
    total_warnings = sum(int(row.get("warnings", 0) or 0) for row in summaries)
    total_infos = sum(int(row.get("infos", 0) or 0) for row in summaries)
    print("doctor results:")
    for row in summaries:
        print(format_doctor_summary(row))
    print(
        f"doctor summary: samples={len(roots)} checked={len(summaries)} failed={failed} "
        f"diagnostics={total_errors}/{total_warnings}/{total_infos} reports={args.report_dir}"
    )
    return 1 if failed else 0


def cmd_install(args: argparse.Namespace) -> int:
    selected_inputs = [bool(args.root), bool(args.bundle), bool(getattr(args, "candidate", None))]
    if sum(1 for selected in selected_inputs if selected) != 1:
        raise SystemExit("install requires exactly one of --root, --bundle or --candidate")
    if getattr(args, "candidate", None):
        try:
            bundle_path, _bundle, bundle_info, previous, candidate = app_registry.validate_install_candidate(
                args.store,
                args.candidate,
                app_registry.DEFAULT_MAX_BUNDLE_BYTES,
                allow_untrusted=getattr(args, "allow_untrusted_signature", False),
                allow_downgrade=getattr(args, "allow_downgrade", False),
            )
        except SystemExit as error:
            if args.report:
                try:
                    candidate = app_registry.load_install_candidate(args.candidate)
                    bundle_path = candidate["bundlePath"]
                    bundle = app_registry.read_bundle(bundle_path, app_registry.DEFAULT_MAX_BUNDLE_BYTES)
                    bundle_info = app_registry.parse_jfapp(bundle)
                    previous = app_registry.existing_app_entry(args.store, bundle_info["summary"]["id"])
                    reason = str(error).removeprefix("jellyframe_app_registry: ").split(":")[0].strip()
                    transaction = app_registry.build_failed_install_transaction_report(
                        args.store,
                        bundle_path,
                        bundle_info,
                        previous,
                        reason or "candidate-rejected",
                        source_kind="install-candidate",
                        preflight_report=str(args.candidate),
                        allow_downgrade=getattr(args, "allow_downgrade", False),
                    )
                    write_install_transaction_report(args.report, transaction, merge=False)
                except SystemExit:
                    pass
            raise
        entry = app_registry.install_bundle(
            args.store,
            bundle_path,
            app_registry.DEFAULT_MAX_APPS,
            app_registry.DEFAULT_MAX_BUNDLE_BYTES,
            allow_downgrade=getattr(args, "allow_downgrade", False),
        )
        if args.report:
            transaction = app_registry.build_install_transaction_report(
                args.store,
                bundle_path,
                entry,
                previous,
                source_kind="install-candidate",
                preflight_report=str(args.candidate),
                allow_downgrade=getattr(args, "allow_downgrade", False),
            )
            transaction["candidate"] = {
                "path": str(args.candidate),
                "signatureStatus": app_registry.candidate_signature_status(candidate),
                "userApproval": bool(candidate.get("userApproval")),
                "download": candidate.get("download", {}) if isinstance(candidate.get("download", {}), dict) else {},
            }
            write_install_transaction_report(args.report, transaction, merge=False)
        print(f"installed-candidate {entry['id']} {entry['versionName']} ({entry['bundleSize']} bytes)")
        return 0
    if args.root:
        if args.report is None:
            args.report = args.store / "last-install.report.json"
        with tempfile.TemporaryDirectory(prefix="jellyframe-install-") as directory:
            args.output_cpp = None
            args.output_bundle = Path(directory) / "app.jfapp"
            args.debug_dir = None
            package_result = cmd_package(args)
            if package_result != 0:
                return package_result
            bundle = app_registry.read_bundle(args.output_bundle, app_registry.DEFAULT_MAX_BUNDLE_BYTES)
            bundle_info = app_registry.parse_jfapp(bundle)
            previous = app_registry.existing_app_entry(args.store, bundle_info["summary"]["id"])
            decision = app_registry.update_policy_decision(
                previous,
                bundle_info["summary"],
                getattr(args, "allow_downgrade", False),
            )
            if not decision["allowed"]:
                transaction = app_registry.build_failed_install_transaction_report(
                    args.store,
                    args.output_bundle,
                    bundle_info,
                    previous,
                    decision["reason"],
                    source_kind="source",
                    preflight_report=str(args.report),
                    allow_downgrade=getattr(args, "allow_downgrade", False),
                )
                write_install_transaction_report(args.report, transaction, merge=True)
                raise SystemExit(
                    "downgrade install is blocked: "
                    f"{bundle_info['summary']['id']} {decision['incomingVersionCode']} < "
                    f"{decision['previousVersionCode']}; use --allow-downgrade or rollback"
                )
            entry = app_registry.install_bundle(
                args.store,
                args.output_bundle,
                app_registry.DEFAULT_MAX_APPS,
                app_registry.DEFAULT_MAX_BUNDLE_BYTES,
                allow_downgrade=getattr(args, "allow_downgrade", False),
            )
            transaction = app_registry.build_install_transaction_report(
                args.store,
                args.output_bundle,
                entry,
                previous,
                source_kind="source",
                preflight_report=str(args.report),
                allow_downgrade=getattr(args, "allow_downgrade", False),
            )
            write_install_transaction_report(args.report, transaction, merge=True)
            print(f"installed {entry['id']} {entry['versionName']} ({entry['bundleSize']} bytes)")
            return 0
    bundle = app_registry.read_bundle(args.bundle, app_registry.DEFAULT_MAX_BUNDLE_BYTES)
    bundle_info = app_registry.parse_jfapp(bundle)
    previous = app_registry.existing_app_entry(args.store, bundle_info["summary"]["id"])
    decision = app_registry.update_policy_decision(
        previous,
        bundle_info["summary"],
        getattr(args, "allow_downgrade", False),
    )
    if not decision["allowed"]:
        if args.report:
            transaction = app_registry.build_failed_install_transaction_report(
                args.store,
                args.bundle,
                bundle_info,
                previous,
                decision["reason"],
                allow_downgrade=getattr(args, "allow_downgrade", False),
            )
            write_install_transaction_report(args.report, transaction, merge=False)
        raise SystemExit(
            "downgrade install is blocked: "
            f"{bundle_info['summary']['id']} {decision['incomingVersionCode']} < "
            f"{decision['previousVersionCode']}; use --allow-downgrade or rollback"
        )
    entry = app_registry.install_bundle(
        args.store,
        args.bundle,
        app_registry.DEFAULT_MAX_APPS,
        app_registry.DEFAULT_MAX_BUNDLE_BYTES,
        allow_downgrade=getattr(args, "allow_downgrade", False),
    )
    if args.report:
        transaction = app_registry.build_install_transaction_report(
            args.store,
            args.bundle,
            entry,
            previous,
            allow_downgrade=getattr(args, "allow_downgrade", False),
        )
        write_install_transaction_report(args.report, transaction, merge=False)
    print(f"installed {entry['id']} {entry['versionName']} ({entry['bundleSize']} bytes)")
    return 0


def add_manifest_package_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--root", required=True, type=Path, help="App package source directory.")
    parser.add_argument("--report", required=True, type=Path, help="Output JSON report path.")
    parser.add_argument("--namespace", default="jellyframe_esp32s3", help="Generated C++ namespace.")
    parser.add_argument("--include", default="jellyframe_esp32s3_resources.h", help="Generated C++ include.")
    parser.add_argument("--target", help="Optional target preset id.")


def add_common_package_args(parser: argparse.ArgumentParser) -> None:
    add_manifest_package_args(parser)
    parser.add_argument("--build-dir", default=default_build_dir(), type=Path, help="Directory containing built tools.")
    parser.add_argument("--skip-check", action="store_true", help="Skip developer preflight checks.")
    parser.add_argument("--strict", action="store_true", help="Fail when diagnostics contain warnings.")
    parser.add_argument("--runtime-log", type=Path,
                        help="Optional Win32 frame-script or runtime capture log to merge into the report.")
    parser.add_argument("--port-telemetry", type=Path,
                        help="Optional real-device port telemetry JSON or 'port_telemetry key=value' log.")


def add_font_preflight_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--no-font-check", action="store_true",
                        help="Skip the default font resource preflight.")
    parser.add_argument("--font-budget",
                        help="Glyph size such as 16x16 for font budget estimates. Defaults to 16x16.")
    parser.add_argument("--font-coverage", type=Path,
                        help="Optional embedded font coverage text file for preflight checks.")
    parser.add_argument("--emit-used-chars", type=Path,
                        help="Optional output file for used non-ASCII characters.")
    parser.add_argument("--font-subset", default="auto",
                        help="Font subset plan: auto, off, or an explicit used-chars output path. Defaults to auto.")
    parser.add_argument("--font-source-bdf", type=Path,
                        help="Optional BDF source used to generate a .jffont supplement during preflight.")
    parser.add_argument("--font-output", type=Path,
                        help="Optional generated .jffont output path. Requires --font-source-bdf.")
    parser.add_argument("--font-coverage-bits", type=int, default=1, choices=[1, 2, 4],
                        help="Coverage depth for generated .jffont supplements.")
    parser.add_argument("--font-allow-missing", action="store_true",
                        help="Allow missing BDF glyphs while generating a .jffont supplement.")


def add_responsive_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--targets",
                        help="Comma-separated target preset ids for responsive profile validation.")
    parser.add_argument("--all-targets", action="store_true",
                        help="Run responsive profile validation for every target preset.")


def main() -> int:
    parser = argparse.ArgumentParser(description="JellyFrame developer CLI.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate = subparsers.add_parser("validate", help="Validate a JellyFrame app package.")
    add_manifest_package_args(validate)
    validate.set_defaults(func=cmd_validate)

    package = subparsers.add_parser("package", help="Generate a resource table and report.")
    add_common_package_args(package)
    package.add_argument("--output-cpp", type=Path, help="Generated C++ resource table.")
    package.add_argument("--output-bundle", type=Path, help="Generated installable .jfapp bundle.")
    package.add_argument("--debug-dir", type=Path, help="Optional copied debug package directory.")
    add_font_preflight_args(package)
    add_responsive_args(package)
    package.set_defaults(func=cmd_package)

    preview = subparsers.add_parser("preview", help="Render an app package through the Win32 shell capture path.")
    preview.add_argument("--root", required=True, type=Path, help="App package source directory.")
    preview.add_argument("--output", required=True, type=Path, help="Output BMP/PPM path.")
    preview.add_argument("--report", type=Path, help="Output JSON report path. Defaults beside --output.")
    preview.add_argument("--build-dir", default=default_build_dir(), type=Path, help="Directory containing built tools.")
    preview.add_argument("--target", help="Optional target preset id used for viewport defaults.")
    preview.add_argument("--width", type=int, default=0, help="Optional viewport width override.")
    preview.add_argument("--height", type=int, default=0, help="Optional viewport height override.")
    preview.add_argument("--runtime-log", type=Path,
                         help="Optional Win32 frame-script or runtime capture log to merge into the report.")
    preview.add_argument("--port-telemetry", type=Path,
                         help="Optional real-device port telemetry JSON or 'port_telemetry key=value' log.")
    add_font_preflight_args(preview)
    add_responsive_args(preview)
    preview.add_argument("--namespace", default="jellyframe_esp32s3", help=argparse.SUPPRESS)
    preview.add_argument("--include", default="jellyframe_esp32s3_resources.h", help=argparse.SUPPRESS)
    preview.add_argument("--skip-check", action="store_true", help="Skip developer preflight checks.")
    preview.add_argument("--strict", action="store_true", help="Fail when diagnostics contain warnings.")
    preview.set_defaults(func=cmd_preview)

    check = subparsers.add_parser("check", help="Validate package and run pipeline/font preflight.")
    add_common_package_args(check)
    add_font_preflight_args(check)
    add_responsive_args(check)
    check.set_defaults(func=cmd_check)

    font = subparsers.add_parser("font", help="Collect package characters and optionally generate bitmap font packs.")
    add_common_package_args(font)
    font.add_argument("--used-chars", required=True, type=Path, help="Output file for used non-ASCII characters.")
    font.add_argument("--font-budget", default="16x16", help="Glyph size such as 16x16 for font budget estimates.")
    font.add_argument("--font-coverage", type=Path, help="Optional embedded font coverage text file.")
    font.add_argument("--bdf", type=Path, help="Optional BDF source font for bitmap pack generation.")
    font.add_argument("--output-header", type=Path, help="Generated C++ BitmapFont header path.")
    font.add_argument("--output-binary", type=Path, help="Generated .jffont bitmap font supplement path.")
    font.add_argument("--name", default="jellyframe_embedded_font", help="Generated C++ font symbol name.")
    font.add_argument("--coverage-bits",
                      type=int,
                      default=1,
                      choices=[1, 2, 4],
                      help="Glyph coverage depth for generated bitmap fonts: 1, 2 or 4 bits per pixel.")
    font.add_argument("--allow-missing", action="store_true", help="Allow missing BDF glyphs when generating font packs.")
    font.set_defaults(func=cmd_font)

    schema = subparsers.add_parser("schema", help="Print the JellyFrame app manifest JSON schema.")
    schema.add_argument("--print-path", action="store_true", help="Print only the schema file path.")
    schema.set_defaults(func=cmd_schema)

    targets = subparsers.add_parser("targets", help="List available target presets.")
    targets.add_argument("--json", action="store_true", help="Print presets as JSON.")
    targets.set_defaults(func=cmd_targets)

    templates = subparsers.add_parser("templates", help="List available app templates.")
    templates.add_argument("--json", action="store_true", help="Print templates as JSON.")
    templates.set_defaults(func=cmd_templates)

    new = subparsers.add_parser("new", help="Create a new source package from a template.")
    new.add_argument("--template", required=True, choices=list_app_templates(), help="Template name.")
    new.add_argument("--output", required=True, type=Path, help="Destination directory; must be missing or empty.")
    new.add_argument("--id", dest="app_id", help="Manifest app id override.")
    new.add_argument("--name", help="Manifest display name override.")
    new.add_argument("--target", help="Optional target preset applied to manifest viewport and targets.")
    new.set_defaults(func=cmd_new)

    install = subparsers.add_parser("install", help="Validate, package and install an app into a desktop registry.")
    install.add_argument("--store", required=True, type=Path, help="Installed-app registry directory.")
    install.add_argument("--root", type=Path, help="Source app package directory. Runs validation and pipeline diagnostics.")
    install.add_argument("--bundle", type=Path, help="Existing .jfapp bundle to install.")
    install.add_argument("--candidate", type=Path, help="Host-prepared install candidate JSON.")
    install.add_argument("--report", type=Path, help="Output JSON report path for --root installs.")
    install.add_argument("--build-dir", default=default_build_dir(), type=Path, help="Directory containing built tools.")
    install.add_argument("--target", help="Optional target preset id used for package diagnostics.")
    install.add_argument("--namespace", default="jellyframe_esp32s3", help=argparse.SUPPRESS)
    install.add_argument("--include", default="jellyframe_esp32s3_resources.h", help=argparse.SUPPRESS)
    install.add_argument("--skip-check", action="store_true", help="Skip developer preflight checks.")
    install.add_argument("--strict", action="store_true", help="Fail when diagnostics contain warnings.")
    install.add_argument("--allow-downgrade", action="store_true",
                         help="Allow installing a lower versionCode over the current app.")
    install.add_argument("--allow-untrusted-signature", action="store_true",
                         help="Permit unsigned/untrusted install candidates for desktop bring-up only.")
    add_font_preflight_args(install)
    add_responsive_args(install)
    install.set_defaults(func=cmd_install)

    registry = subparsers.add_parser("registry", help="Manage a desktop installed-app registry mock.")
    registry.add_argument("registry_args", nargs=argparse.REMAINDER,
                          help="Arguments passed to tools/app_registry.py.")
    registry.set_defaults(func=cmd_registry)

    doctor = subparsers.add_parser("doctor", help="Run repository self-checks for trial-ready sample packages.")
    doctor.add_argument("--build-dir", default=default_build_dir(), type=Path,
                        help="Directory containing built tools.")
    doctor.add_argument("--samples-dir", default=repo_root() / "samples" / "apps" / "packages", type=Path,
                        help="Directory containing source-package samples.")
    doctor.add_argument("--report-dir", default=repo_root() / "build" / "doctor_reports", type=Path,
                        help="Directory for per-sample JSON reports.")
    doctor.add_argument("--sample", action="append",
                        help="Only check named sample package(s). May be repeated or comma-separated.")
    doctor.add_argument("--exclude-sample", action="append",
                        help="Skip named sample package(s). May be repeated or comma-separated.")
    doctor.add_argument("--target", help="Primary target preset id passed to package diagnostics.")
    doctor.add_argument("--targets", default="round-300,rect-320x240,rect-172x320",
                        help="Comma-separated responsive target preset ids.")
    doctor.add_argument("--no-font-check", action="store_true",
                        help="Skip font resource preflight while checking samples.")
    doctor.add_argument("--strict", action="store_true",
                        help="Fail when sample diagnostics contain warnings.")
    doctor.add_argument("--fail-fast", action="store_true",
                        help="Stop after the first failed sample.")
    doctor.set_defaults(func=cmd_doctor)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
