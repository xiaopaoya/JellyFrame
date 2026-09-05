#!/usr/bin/env python3
import sys
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

    print("SDK archive extraction tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
