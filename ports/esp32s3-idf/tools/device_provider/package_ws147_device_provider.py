#!/usr/bin/env python3
"""Create a self-contained WS147 Developer Image and provider archive."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

from provider_version import PROVIDER_VERSION


ROOT = Path(__file__).resolve().parents[4]
PROVIDER_SOURCE = Path(__file__).resolve().parent
FEATURE_FAMILIES = [
    "core.document", "core.paint", "css.flex-grid", "css.modern-paint",
    "forms.advanced", "graphics.canvas2d",
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--factory-image", required=True, type=Path)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--image-version", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    firmware = args.firmware.resolve()
    factory = args.factory_image.resolve()
    if not firmware.is_file() or not factory.is_file():
        raise SystemExit("firmware and factory image must exist")
    if len(args.source_revision) != 40 or any(char not in "0123456789abcdef" for char in args.source_revision):
        raise SystemExit("source revision must be 40 lowercase hexadecimal characters")
    if factory.stat().st_size != 16 * 1024 * 1024:
        raise SystemExit("factory image must be a complete 16 MiB flash image")

    output = args.output.resolve()
    archive_name = f"jellyframe-ws147-developer-{args.image_version}-provider-{PROVIDER_VERSION}"
    release = output / archive_name
    if release.exists():
        raise SystemExit(f"output already exists: {release}")

    manifest = {
        "format": "jellyframe.device-image", "formatVersion": 0,
        "imageId": "org.jellyframe.ws147.developer", "imageVersion": args.image_version,
        "runtimeVersion": "0.6.0-dev", "renderCore": {"version": "0.6.1", "abi": 1},
        "source": {"revision": args.source_revision, "firmwareSha256": sha256(firmware)},
        "board": {"id": "ws147", "display": {"width": 172, "height": 320, "shape": "rect"}},
        "profile": {"id": "rect-172x320", "featureFamilies": FEATURE_FAMILIES},
        "transport": {"protocol": "JFDP/1", "kind": "usb-serial-jtag"},
        "storage": {"maxBundleBytes": 327680},
        "recovery": {"procedureId": "ws147-usb-recovery-v1", "factoryImageSha256": sha256(factory)},
    }

    with tempfile.TemporaryDirectory(prefix="jellyframe-provider-release-") as temporary:
        staging = Path(temporary) / archive_name
        provider = staging / "provider"
        image = staging / "developer-image"
        recovery = staging / "recovery"
        provider.mkdir(parents=True)
        image.mkdir()
        recovery.mkdir()
        for name in ("jellyframe_device.py", "jellyframe-device.cmd", "provider_version.py", "requirements.txt", "README.md"):
            copy(PROVIDER_SOURCE / name, provider / name)
        copy(ROOT / "tools" / "device_image_manifest.py", provider / "lib" / "device_image_manifest.py")
        config = {
            "endpointId": "ws147-developer-local", "port": "COMx", "baud": 115200,
            "manifest": "../developer-image/ws147-developer-image.manifest.json",
        }
        write_json(provider / "jellyframe-device.config.example.json", config)
        write_json(provider / "provider-release.json", {
            "format": "jellyframe.device-provider-release", "formatVersion": 0,
            "id": "jellyframe-device", "version": PROVIDER_VERSION,
            "protocol": "JFDP/1", "board": "ws147", "python": ">=3.10", "dependency": "pyserial==3.5",
        })
        copy(firmware, image / "jellyframe_esp32s3_bench.bin")
        write_json(image / "ws147-developer-image.manifest.json", manifest)
        copy(factory, recovery / "ws147-factory-16mb.bin")
        copy(PROVIDER_SOURCE / "RECOVERY.md", recovery / "RECOVERY.md")
        (staging / "README.md").write_text(
            "# JellyFrame WS147 Developer Image\n\n"
            f"Provider `jellyframe-device` version: `{PROVIDER_VERSION}`.\n\n"
            "1. Install Python 3.10+ and run `python -m pip install -r provider/requirements.txt`.\n"
            "2. Copy `provider/jellyframe-device.config.example.json` to `provider/jellyframe-device.config.json`, "
            "replace `COMx`, and leave the relative manifest path intact.\n"
            "3. Configure VS Code `jellyframe.deviceProvider` with the absolute path to "
            "`provider/jellyframe-device.cmd`, and `jellyframe.deviceManifest` with the absolute path to "
            "`developer-image/ws147-developer-image.manifest.json`.\n"
            "4. Run JellyFrame: Discover Device, then JellyFrame: Device Info.\n\n"
            "The VS Code extension never bundles this provider and does not infer a serial endpoint.\n",
            encoding="utf-8")
        hashes = []
        for path in sorted(value for value in staging.rglob("*") if value.is_file()):
            hashes.append(f"{sha256(path)}  {path.relative_to(staging).as_posix()}")
        (staging / "SHA256SUMS.txt").write_text("\n".join(hashes) + "\n", encoding="ascii")
        output.mkdir(parents=True, exist_ok=True)
        shutil.copytree(staging, release)
        zip_path = output / f"{archive_name}.zip"
        with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(value for value in release.rglob("*") if value.is_file()):
                archive.write(path, path.relative_to(output))

    print(json.dumps({"release": str(release), "archive": str(zip_path), "providerVersion": PROVIDER_VERSION,
                      "firmwareSha256": manifest["source"]["firmwareSha256"],
                      "factoryImageSha256": manifest["recovery"]["factoryImageSha256"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
