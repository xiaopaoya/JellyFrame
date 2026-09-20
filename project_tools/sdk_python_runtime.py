"""Build-time acquisition of the pinned, isolated Windows author interpreter."""

import hashlib
import json
import os
import tempfile
import urllib.request
import zipfile
from pathlib import Path

PYTHON_VERSION = "3.13.15"
ARTIFACTS = (
    {
        "name": "python-3.13.15-embed-amd64.zip",
        "url": "https://www.python.org/ftp/python/3.13.15/python-3.13.15-embed-amd64.zip",
        "sha256": "d1f04d990aee1253d8569e8e5104e30fa9f5fa830899f14843448872d936a2cf",
    },
    {
        "name": "pyserial-3.5-py2.py3-none-any.whl",
        "url": "https://files.pythonhosted.org/packages/07/bc/587a445451b253b285629263eb51c2d8e9bcea4fc97826266d186f96f558/pyserial-3.5-py2.py3-none-any.whl",
        "sha256": "c4451db6ba391ca6ca299fb3ec7bae67a5c55dde170964c7a14ceefec02f2cf0",
    },
)


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def acquire(artifact: dict[str, str], cache: Path) -> Path:
    cache.mkdir(parents=True, exist_ok=True)
    target = cache / artifact["name"]
    if target.is_file():
        if digest(target) != artifact["sha256"]:
            raise ValueError(f"cached runtime artifact failed SHA-256: {target}")
        return target
    fd, temporary = tempfile.mkstemp(prefix=".sdk-download-", dir=cache)
    try:
        with os.fdopen(fd, "wb") as output, urllib.request.urlopen(artifact["url"], timeout=60) as source:
            total = 0
            while chunk := source.read(65536):
                total += len(chunk)
                if total > 32 * 1024 * 1024:
                    raise ValueError("runtime download exceeds size limit")
                output.write(chunk)
        if digest(Path(temporary)) != artifact["sha256"]:
            raise ValueError(f"runtime artifact failed SHA-256: {artifact['name']}")
        os.replace(temporary, target)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return target


def install_runtime(destination: Path, cache: Path) -> dict[str, object]:
    destination.mkdir(parents=True, exist_ok=False)
    for artifact, subdirectory in zip(ARTIFACTS, (destination, destination / "Lib" / "site-packages")):
        # Only the exact hash-pinned upstream archives are extracted here.
        with zipfile.ZipFile(acquire(artifact, cache)) as archive:
            archive.extractall(subdirectory)
    (destination / "python313._pth").write_text(
        "python313.zip\n.\nLib/site-packages\n../..\n../../tools\n../../tools/debug\n",
        encoding="ascii",
    )
    metadata = {
        "implementation": "CPython", "version": PYTHON_VERSION, "platform": "win32-x64",
        "executable": "runtime/python/python.exe", "dependencies": {"pyserial": "3.5"},
        "artifacts": list(ARTIFACTS),
    }
    (destination / "provenance.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return metadata
