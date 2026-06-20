#!/usr/bin/env python3
import argparse
import json
import re
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


def write_json_report(path: Path, report: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
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


def remember_pipeline_report(args: argparse.Namespace, pipeline_report_path: Path) -> None:
    args._pipeline_report = load_json_if_exists(pipeline_report_path)
    merge_pipeline_report(args.report, args._pipeline_report)


def diagnostic_status_from_report(package_report_path: Path) -> tuple[int, int, int]:
    report = load_json_if_exists(package_report_path)
    pipeline = report.get("pipelineDiagnostics", {})
    summary = pipeline.get("summary", {}) if isinstance(pipeline, dict) else {}
    errors = int(summary.get("error", 0) or 0)
    warnings = int(summary.get("warning", 0) or 0)
    package_warnings = report.get("warnings", [])
    if isinstance(package_warnings, list):
        warnings += len(package_warnings)
    infos = int(summary.get("info", 0) or 0)
    return errors, warnings, infos


def enforce_diagnostics_policy(args: argparse.Namespace) -> int:
    errors, warnings, infos = diagnostic_status_from_report(args.report)
    print(f"diagnostic policy: errors={errors} warnings={warnings} info={infos}")
    if errors > 0:
        return 1
    if getattr(args, "strict", False) and warnings > 0:
        return 1
    return 0


def run_pipeline_check(args: argparse.Namespace) -> int:
    pseudo_browser = tool_path(args.build_dir, "jellyframe_pseudo_browser")
    ensure_tool(pseudo_browser)
    manifest_path = args.root / "jellyframe.app.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    entry = str(manifest.get("entry", "/index.html"))
    entry_path = args.root / Path(*entry.lstrip("/").split("/"))
    target = getattr(args, "target", None)
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
            remember_pipeline_report(args, diagnostics_json)
        return result


def run_package_preflight(args: argparse.Namespace, include_pipeline: bool) -> int:
    validate_result = cmd_validate(args)
    if validate_result != 0 or getattr(args, "skip_check", False):
        return validate_result
    if include_pipeline:
        pipeline_result = run_pipeline_check(args)
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
    for resource in report.get("resources", []):
        if resource.get("kind") not in {"Stylesheet", "ClassicScript", "Other"}:
            continue
        resource_path = resource.get("path", "")
        if not resource_path:
            continue
        relative = resource_path[1:] if resource_path.startswith("/") else resource_path
        files.append(str(root / Path(*relative.split("/"))))
    return files


def run_font_resource_check(args: argparse.Namespace) -> int:
    font_check = tool_path(args.build_dir, "jellyframe_font_resource_check")
    ensure_tool(font_check)
    files = resource_files_from_report(args.root, args.report)
    command = [str(font_check)]
    emit_used_chars = getattr(args, "emit_used_chars", None)
    font_coverage = getattr(args, "font_coverage", None)
    if font_coverage:
        command.extend(["--font-coverage", str(font_coverage)])
    command.extend(["--font-budget", effective_font_budget(args)])
    if emit_used_chars:
        command.extend(["--emit-used-chars", str(emit_used_chars)])
    command.extend(files)
    return run_command(command)


def cmd_check(args: argparse.Namespace) -> int:
    validate_result = cmd_validate(args)
    if validate_result != 0:
        return validate_result
    if not getattr(args, "skip_check", False):
        pipeline_result = run_pipeline_check(args)
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
            print("Text-search compatibility scanning has been retired; font resource preflight was skipped by request.")
        else:
            print("Text-search compatibility scanning has been retired; font resource preflight ran with the package check.")
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
    preview.add_argument("--namespace", default="jellyframe_esp32s3", help=argparse.SUPPRESS)
    preview.add_argument("--include", default="jellyframe_esp32s3_resources.h", help=argparse.SUPPRESS)
    preview.add_argument("--skip-check", action="store_true", help="Skip developer preflight checks.")
    preview.add_argument("--strict", action="store_true", help="Fail when diagnostics contain warnings.")
    preview.set_defaults(func=cmd_preview)

    check = subparsers.add_parser("check", help="Validate package and run pipeline/font preflight.")
    add_common_package_args(check)
    add_font_preflight_args(check)
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
    install.set_defaults(func=cmd_install)

    registry = subparsers.add_parser("registry", help="Manage a desktop installed-app registry mock.")
    registry.add_argument("registry_args", nargs=argparse.REMAINDER,
                          help="Arguments passed to tools/app_registry.py.")
    registry.set_defaults(func=cmd_registry)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
