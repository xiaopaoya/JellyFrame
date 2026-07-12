import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run(command: list[str]) -> str:
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    require(result.returncode == 0, result.stdout)
    return result.stdout


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: template_trial_tests.py PATH_TO_BUILT_TOOLS")
        return 2

    build_dir = Path(sys.argv[1])
    require(build_dir.is_dir(), f"missing built tools directory: {build_dir}")
    cli = REPO_ROOT / "tools" / "jellyframe_cli.py"
    templates = json.loads(run([sys.executable, str(cli), "templates", "--json"]).strip()).get("templates", [])
    require(isinstance(templates, list) and templates, "CLI did not expose any app templates")

    with tempfile.TemporaryDirectory(prefix="jellyframe-template-trial-") as directory:
        root = Path(directory)
        for template in templates:
            require(isinstance(template, str) and template, "template name must be a non-empty string")
            package_root = root / template
            run([
                sys.executable,
                str(cli),
                "new",
                "--template",
                template,
                "--output",
                str(package_root),
                "--id",
                f"org.jellyframe.trial.{template}",
            ])
            manifest = json.loads((package_root / "jellyframe.app.json").read_text(encoding="utf-8-sig"))
            targets = manifest.get("targets", {})
            require(isinstance(targets, dict) and targets, f"{template} template has no declared target")
            target = sorted(targets)[0]
            report_path = package_root / "trial.report.json"
            run([
                sys.executable,
                str(cli),
                "check",
                "--root",
                str(package_root),
                "--build-dir",
                str(build_dir),
                "--target",
                target,
                "--report",
                str(report_path),
            ])
            report = json.loads(report_path.read_text(encoding="utf-8-sig"))
            warnings = report.get("warnings", [])
            require(not warnings, f"{template} template has package warnings: {warnings}")
            diagnostics = report.get("pipelineDiagnostics", {}).get("diagnostics", [])
            blocking = [
                entry for entry in diagnostics
                if isinstance(entry, dict) and entry.get("severity") in {"warning", "error"}
            ]
            require(not blocking,
                    f"{template} template has default-target diagnostics: {blocking}")

    print(f"template trial tests passed ({len(templates)} templates)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
