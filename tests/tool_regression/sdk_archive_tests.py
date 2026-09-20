#!/usr/bin/env python3
import sys
import json
import os
import subprocess
import tempfile
import zipfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "vscode-jellyframe"))
from sdk_archive import extract  # noqa: E402


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="jellyframe-sdk-archive-test-") as directory:
        root = Path(directory)
        valid = root / "valid.zip"
        with zipfile.ZipFile(valid, "w") as archive:
            archive.writestr("jellyframe-app-sdk-0.6.0/tools/jellyframe_cli.py", "# test\n")
        extracted = root / "valid-out"
        assert extract(valid, extracted) == "jellyframe-app-sdk-0.6.0"
        assert (extracted / "jellyframe-app-sdk-0.6.0/tools/jellyframe_cli.py").is_file()

        traversal = root / "traversal.zip"
        with zipfile.ZipFile(traversal, "w") as archive:
            archive.writestr("jellyframe-app-sdk-0.6.0/tools/jellyframe_cli.py", "# test\n")
            archive.writestr("jellyframe-app-sdk-0.6.0/../../outside.txt", "rejected\n")
        try:
            extract(traversal, root / "traversal-out")
        except ValueError as error:
            assert "unsafe member path" in str(error)
        else:
            raise AssertionError("path traversal archive was accepted")
        assert not (root / "outside.txt").exists()

        if sys.platform == "win32":
            powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"

            def windows_extract(archive, destination):
                return subprocess.run([str(powershell), "-NoProfile", "-NonInteractive", "-ExecutionPolicy",
                                       "Bypass", "-File", str(REPO_ROOT / "tools/vscode-jellyframe/sdk_archive.ps1"),
                                       "-Archive", str(archive), "-Destination", str(destination)],
                                      text=True, capture_output=True, timeout=30)

            result = windows_extract(valid, root / "windows valid out")
            assert result.returncode == 0, result.stderr
            assert json.loads(result.stdout)["root"] == "jellyframe-app-sdk-0.6.0"
            assert windows_extract(valid, root / "windows valid out").returncode != 0
            for index, unsafe in enumerate(("../outside", "/absolute", "C:/drive", "sdk/x:ads",
                                            "sdk/CON.txt", "sdk/file.", "sdk/file ", "sdk/../outside",
                                            "other/file", "sdk/TOOLS/jellyframe_cli.py", "sdk/\\absolute")):
                bad = root / f"bad-{index}.zip"
                with zipfile.ZipFile(bad, "w") as archive:
                    archive.writestr("sdk/tools/jellyframe_cli.py", "# test")
                    archive.writestr(unsafe, "bad")
                result = windows_extract(bad, root / f"bad-{index}-out")
                assert result.returncode != 0, unsafe
            symlink = root / "symlink.zip"
            with zipfile.ZipFile(symlink, "w") as archive:
                archive.writestr("sdk/tools/jellyframe_cli.py", "# test")
                info = zipfile.ZipInfo("sdk/link")
                info.external_attr = 0o120777 << 16
                archive.writestr(info, "../../outside")
            assert windows_extract(symlink, root / "symlink-out").returncode != 0

    print("SDK archive extraction tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
