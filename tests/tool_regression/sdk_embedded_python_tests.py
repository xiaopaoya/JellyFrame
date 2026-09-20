"""Exercise the distributed SDK without a system Python on the child's PATH."""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def verify(archive: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="sdk isolated smoke ") as directory:
        workspace = Path(directory)
        environment = os.environ.copy()
        environment.update(PATH=str(Path(os.environ["SystemRoot"]) / "System32"),
                           PYTHONHOME=str(workspace / "invalid"), PYTHONPATH=str(workspace / "invalid"))
        powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
        extractor = Path(__file__).resolve().parents[2] / "tools/vscode-jellyframe/sdk_archive.ps1"
        extraction = subprocess.run([str(powershell), "-NoProfile", "-NonInteractive", "-ExecutionPolicy",
                                     "Bypass", "-File", str(extractor), "-Archive", str(archive),
                                     "-Destination", str(workspace)], env=environment,
                                    text=True, capture_output=True, timeout=60)
        assert extraction.returncode == 0, extraction.stderr
        sdk = workspace / json.loads(extraction.stdout)["root"]
        python = sdk / "runtime" / "python" / "python.exe"
        manifest = json.loads((sdk / "sdk-manifest.json").read_text(encoding="utf-8"))
        assert manifest["pythonRuntime"]["executable"] == python.relative_to(sdk).as_posix()
        assert manifest["nativeRuntime"]["kind"] == "msvc-app-local"
        for profile in manifest["desktopProfiles"]:
            for library in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
                assert (sdk / "build" / profile / "Release" / library).is_file()

        def run(*args: str) -> str:
            result = subprocess.run([str(python), *args], cwd=workspace, env=environment,
                                    text=True, capture_output=True, timeout=60)
            assert result.returncode == 0, result.stdout + result.stderr
            return result.stdout

        run("-c", "import sys, ssl, serial; from serial.tools import list_ports; "
            "assert sys.flags.isolated; assert serial.VERSION == '3.5'")
        cli = str(sdk / "tools" / "jellyframe_cli.py")
        assert "blank" in json.loads(run(cli, "templates", "--json"))["templates"]
        app = workspace / "test app"
        run(cli, "new", "--template", "blank", "--output", str(app), "--id", "org.example.isolated",
            "--name", "Isolated", "--target", "round-300")
        run(cli, "validate", "--root", str(app), "--report", str(workspace / "validation.json"))
        run(cli, "check", "--root", str(app), "--target", "round-300",
            "--report", str(workspace / "check.json"))
        run(cli, "package", "--root", str(app), "--target", "round-300",
            "--output-bundle", str(workspace / "test.jfapp"), "--report", str(workspace / "package.json"))
        assert (workspace / "test.jfapp").is_file()
        run(cli, "preview", "--root", str(app), "--target", "round-300",
            "--output", str(workspace / "preview.ppm"), "--report", str(workspace / "preview.json"))
        assert (workspace / "preview.ppm").is_file()
        run(cli, "font", "--root", str(app), "--used-chars", str(workspace / "chars.txt"),
            "--report", str(workspace / "font.json"))
        run(str(sdk / "tools" / "render_performance_report.py"), "--report", str(workspace / "check.json"),
            "--output", str(workspace / "performance.json"), "--html-output", str(workspace / "performance.html"))
        # Published providers used bare `python`; test that compatibility as well
        # as the new explicit-interpreter launcher, without touching a device.
        for launcher in ('python', '"%JELLYFRAME_PYTHON%"'):
            provider = workspace / "test provider.cmd"
            provider.write_text(f'@echo off\n{launcher} "%~dp0provider.py" %*\n', encoding="ascii")
            (workspace / "provider.py").write_text(
                "import json, serial, sys\nassert serial.VERSION == '3.5'\n"
                "print(json.dumps({'format':'jellyframe.device-provider','formatVersion':0,"
                "'kind':'result','operation':'discover','requestId':sys.argv[sys.argv.index('--request-id')+1],"
                "'resultCode':'ok','provider':{'id':'test','version':'0.1'}}))\n", encoding="ascii")
            # Reuse the actual invocation path, including its child environment.
            run("-c", "from pathlib import Path; from device_provider_client import invoke_provider; "
                "assert invoke_provider(Path(__import__('sys').argv[1]), 'discover')['resultCode'] == 'ok'",
                str(provider))
        run(str(sdk / "tools" / "debug" / "jellyframe_debug.py"), "--help")
    print("Embedded SDK Python isolated smoke passed")


if __name__ == "__main__":
    verify(Path(sys.argv[1]).resolve())
