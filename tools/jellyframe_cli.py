#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
from pathlib import Path


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
    path = repo_root() / "schemas" / "jellyframe.app.schema.json"
    if not path.is_file():
        raise SystemExit(f"missing schema: {path}")
    return path


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
        command.extend(["--output-cpp", str(args.output_cpp)])
        if args.debug_dir:
            command.extend(["--debug-dir", str(args.debug_dir)])
    if args.namespace:
        command.extend(["--namespace", args.namespace])
    if args.include:
        command.extend(["--include", args.include])
    return command


def cmd_validate(args: argparse.Namespace) -> int:
    return run_command(package_command(args, True))


def cmd_package(args: argparse.Namespace) -> int:
    return run_command(package_command(args, False))


def cmd_preview(args: argparse.Namespace) -> int:
    pseudo_browser = tool_path(args.build_dir, "jellyframe_pseudo_browser")
    ensure_tool(pseudo_browser)
    command = [
        str(pseudo_browser),
        "--app",
        str(args.root),
        str(args.output),
    ]
    if args.width:
        command.append(str(args.width))
    if args.height:
        command.append(str(args.height))
    if args.pump_timers:
        command.extend(["--pump-timers", str(args.pump_timers)])
    return run_command(command)


def resource_files_from_report(root: Path, report_path: Path) -> list[str]:
    report = json.loads(report_path.read_text(encoding="utf-8"))
    files = []
    for resource in report.get("resources", []):
        resource_path = resource.get("path", "")
        if not resource_path:
            continue
        relative = resource_path[1:] if resource_path.startswith("/") else resource_path
        files.append(str(root / Path(*relative.split("/"))))
    return files


def cmd_check(args: argparse.Namespace) -> int:
    validate_result = cmd_validate(args)
    if validate_result != 0:
        return validate_result
    capability_check = tool_path(args.build_dir, "jellyframe_capability_check")
    ensure_tool(capability_check)
    files = resource_files_from_report(args.root, args.report)
    command = [str(capability_check)]
    if args.font_budget:
        command.extend(["--font-budget", args.font_budget])
    if args.emit_used_chars:
        command.extend(["--emit-used-chars", str(args.emit_used_chars)])
    command.extend(files)
    return run_command(command)


def cmd_schema(args: argparse.Namespace) -> int:
    path = schema_path()
    if args.print_path:
        print(path)
    else:
        print(path.read_text(encoding="utf-8"), end="")
    return 0


def add_common_package_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--root", required=True, type=Path, help="App package source directory.")
    parser.add_argument("--report", required=True, type=Path, help="Output JSON report path.")
    parser.add_argument("--namespace", default="jellyframe_esp32s3", help="Generated C++ namespace.")
    parser.add_argument("--include", default="jellyframe_esp32s3_resources.h", help="Generated C++ include.")


def main() -> int:
    parser = argparse.ArgumentParser(description="JellyFrame developer CLI.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate = subparsers.add_parser("validate", help="Validate a JellyFrame app package.")
    add_common_package_args(validate)
    validate.set_defaults(func=cmd_validate)

    package = subparsers.add_parser("package", help="Generate a resource table and report.")
    add_common_package_args(package)
    package.add_argument("--output-cpp", required=True, type=Path, help="Generated C++ resource table.")
    package.add_argument("--debug-dir", type=Path, help="Optional copied debug package directory.")
    package.set_defaults(func=cmd_package)

    preview = subparsers.add_parser("preview", help="Render an app package through the pseudo browser.")
    preview.add_argument("--root", required=True, type=Path, help="App package source directory.")
    preview.add_argument("--output", required=True, type=Path, help="Output BMP/PPM path.")
    preview.add_argument("--build-dir", default=default_build_dir(), type=Path, help="Directory containing built tools.")
    preview.add_argument("--width", type=int, default=0, help="Optional viewport width override.")
    preview.add_argument("--height", type=int, default=0, help="Optional viewport height override.")
    preview.add_argument("--pump-timers", type=int, default=0, help="Optional timer pump duration in milliseconds.")
    preview.set_defaults(func=cmd_preview)

    check = subparsers.add_parser("check", help="Validate package and run capability checks on packaged files.")
    add_common_package_args(check)
    check.add_argument("--build-dir", default=default_build_dir(), type=Path, help="Directory containing built tools.")
    check.add_argument("--font-budget", help="Optional glyph size such as 16x16 for font budget estimates.")
    check.add_argument("--emit-used-chars", type=Path, help="Optional output file for used non-ASCII characters.")
    check.set_defaults(func=cmd_check)

    schema = subparsers.add_parser("schema", help="Print the JellyFrame app manifest JSON schema.")
    schema.add_argument("--print-path", action="store_true", help="Print only the schema file path.")
    schema.set_defaults(func=cmd_schema)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
