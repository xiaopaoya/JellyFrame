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
        if args.output_cpp:
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
    "style-property-unsupported": {
        "title": "CSS property is outside the supported subset",
        "explanation": "The parser accepted the CSS syntax, but this property is not applied by JellyFrame.",
        "action": "Replace it with a documented subset property from the capability matrix, or move the effect into a supported component/canvas path.",
    },
    "style-declaration-ignored": {
        "title": "CSS declaration value was ignored",
        "explanation": "The property may exist, but the specific value is outside the supported subset.",
        "action": "Use a simpler documented value. For sizing, prefer px, %, min/max/clamp subsets and box-sizing: border-box.",
    },
    "style-after-property-unsupported": {
        "title": "::after uses an unsupported property",
        "explanation": "Generated content exists only as a small decoration/text subset.",
        "action": "Keep ::before/::after to content, simple box styling and low-cost decoration. Put richer styling on a real element.",
    },
    "style-conic-gradient-unsupported": {
        "title": "conic-gradient() is outside the supported subset",
        "explanation": "JellyFrame supports only a small progress-ring-oriented conic-gradient subset.",
        "action": "Use two-color or simple stop conic gradients centered in the element, or use Canvas 2D for custom gauges.",
    },
    "layer-conic-gradient-area-budget": {
        "title": "Conic gradient exceeds the paint budget",
        "explanation": "The gradient area is too expensive for the configured embedded rendering budget.",
        "action": "Shrink the element, simplify the gradient, pre-render the asset, or move the effect behind an opt-in canvas/resource budget.",
    },
    "css-at-rule-skipped": {
        "title": "CSS at-rule was skipped",
        "explanation": "This at-rule is not part of the supported CSS subset.",
        "action": "Use supported @media rules and simple selectors, or move target-specific choices into manifest targets and plain CSS rules.",
    },
    "css-selector-skipped": {
        "title": "CSS selector was skipped",
        "explanation": "The selector is too complex or outside the supported selector subset.",
        "action": "Use simple class, id, element, descendant or documented pseudo-class selectors. Avoid browser-only selector tricks in app UI.",
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
    "script-capability-missing": {
        "title": "JavaScript API is used without a manifest capability",
        "explanation": "The app script references a host-backed Web-near API, but the manifest does not request the matching JellyFrame capability.",
        "action": "Declare the reported capability in jellyframe.app.json, verify the target profile supports it, and keep a visible fallback for hosts that deny it.",
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
    )
    metrics = {key: parsed[key] for key in keys if key in parsed}
    return metrics


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
                                    "",
                                    str(diagnostic.get("detail", "")))

    for profile in report.get("responsiveProfiles", []):
        if not isinstance(profile, dict):
            continue
        target = str(profile.get("target", ""))
        status = str(profile.get("status", ""))
        layout = profile.get("layout", {}) if isinstance(profile.get("layout", {}), dict) else {}
        if status == "horizontal-overflow" or bool(layout.get("horizontalOverflow", False)):
            append_developer_advice(advice,
                                    seen,
                                    "visual-horizontal-overflow",
                                    "warning",
                                    "",
                                    "Responsive profile reports horizontal overflow.",
                                    target)
        if status == "scroll-needed" or bool(layout.get("verticalOverflow", False)):
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
                                    "",
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
    command = [
        str(win32_browser),
        "--capture",
        str(args.output),
        "--app",
        str(args.root),
    ]
    if width:
        command.extend(["--viewport-width", str(width)])
    if height:
        command.extend(["--viewport-height", str(height)])
    result = run_command(command)
    if result == 0:
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
        files.append(str(root / Path(*relative.split("/"))))
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


def sample_package_roots(samples_dir: Path) -> list[Path]:
    if not samples_dir.is_dir():
        raise SystemExit(f"samples directory does not exist: {samples_dir}")
    roots = []
    for path in sorted(samples_dir.iterdir()):
        if path.is_dir() and (path / "jellyframe.app.json").is_file():
            roots.append(path)
    return roots


def doctor_summary_from_report(sample: str, status: str, report_path: Path) -> dict:
    errors, warnings, infos = diagnostic_status_from_report(report_path)
    report = load_json_if_exists(report_path)
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
        f"targets={target_text} report={row.get('report', '')}"
    )


def cmd_doctor(args: argparse.Namespace) -> int:
    ensure_tool(tool_path(args.build_dir, "jellyframe_pseudo_browser"))
    roots = sample_package_roots(args.samples_dir)
    if not roots:
        raise SystemExit(f"no sample packages found in {args.samples_dir}")
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
    if bool(args.root) == bool(args.bundle):
        raise SystemExit("install requires exactly one of --root or --bundle")
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
            return app_registry.main([
                "install",
                "--store",
                str(args.store),
                "--bundle",
                str(args.output_bundle),
            ])
    return app_registry.main([
        "install",
        "--store",
        str(args.store),
        "--bundle",
        str(args.bundle),
    ])


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
    install.add_argument("--report", type=Path, help="Output JSON report path for --root installs.")
    install.add_argument("--build-dir", default=default_build_dir(), type=Path, help="Directory containing built tools.")
    install.add_argument("--target", help="Optional target preset id used for package diagnostics.")
    install.add_argument("--namespace", default="jellyframe_esp32s3", help=argparse.SUPPRESS)
    install.add_argument("--include", default="jellyframe_esp32s3_resources.h", help=argparse.SUPPRESS)
    install.add_argument("--skip-check", action="store_true", help="Skip developer preflight checks.")
    install.add_argument("--strict", action="store_true", help="Fail when diagnostics contain warnings.")
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
