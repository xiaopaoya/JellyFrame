import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CLI = REPO_ROOT / "tools" / "jellyframe_cli.py"


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
    templates = json.loads(run([sys.executable, str(CLI), "templates", "--json"])).get("templates", [])
    require("blank" in templates, "CLI did not expose the blank App template")

    with tempfile.TemporaryDirectory(prefix="jellyframe-blank-template-") as directory:
        output = Path(directory) / "blank-app"
        run([
            sys.executable,
            str(CLI),
            "new",
            "--template",
            "blank",
            "--output",
            str(output),
            "--id",
            "org.example.blank-test",
            "--name",
            "Blank Test",
            "--target",
            "rect-320x240",
        ])

        manifest = json.loads((output / "jellyframe.app.json").read_text(encoding="utf-8"))
        require(manifest["id"] == "org.example.blank-test", "new did not apply the App ID")
        require(manifest["name"] == "Blank Test", "new did not apply the App name")
        require(manifest["targets"]["rect-320x240"]["viewport"] == {
            "width": 320,
            "height": 240,
            "shape": "rect",
        }, "new did not apply the selected target")
        require("Hello world" in (output / "index.html").read_text(encoding="utf-8"),
                "blank HTML lost the Hello world entry point")
        require((output / "styles" / "app.css").read_text(encoding="utf-8").strip() ==
                "/* Add app styles here. */", "blank CSS must remain a single starter comment")
        require((output / "scripts" / "app.js").read_text(encoding="utf-8").strip() ==
                "// Add app behavior here.", "blank JavaScript must remain a single starter comment")

        report = output / "validation.json"
        run([sys.executable, str(CLI), "validate", "--root", str(output), "--report", str(report)])
        report_data = json.loads(report.read_text(encoding="utf-8"))
        require(not report_data.get("warnings"), f"blank template has warnings: {report_data.get('warnings')}")

    print("blank template tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
