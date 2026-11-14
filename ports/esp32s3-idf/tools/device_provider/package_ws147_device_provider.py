#!/usr/bin/env python3
"""Create a self-contained WS147 Developer Image and provider archive."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
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
DEFAULT_RENDER_CORE_VERSION = "0.6.2"
DEFAULT_RENDER_CORE_ABI = 1
VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")


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


def _render_core_from_build_manifest(path: Path) -> tuple[str, int, dict[str, object]]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise SystemExit("build manifest must be valid UTF-8 JSON") from error
    core = None
    for section in ("sources", "actualSourceIdentities"):
        section_value = document.get(section) if isinstance(document, dict) else None
        if isinstance(section_value, dict) and isinstance(section_value.get("renderCore"), dict):
            core = section_value["renderCore"]
            break
    if core is None:
        raise SystemExit("build manifest must contain sources.renderCore metadata")
    version = core.get("packageVersion")
    abi = core.get("engineAbi")
    if (not isinstance(version, str) or VERSION_PATTERN.fullmatch(version) is None or
            not isinstance(abi, int) or isinstance(abi, bool) or abi <= 0):
        raise SystemExit("build manifest has invalid Render Core packageVersion/engineAbi")
    provenance: dict[str, object] = {}
    identity = core.get("sourceIdentity", core.get("sourceHash"))
    input_count = core.get("sourceInputCount", core.get("sourceFileCount"))
    if identity is not None:
        if not isinstance(identity, str) or re.fullmatch(r"[0-9a-f]{64}", identity) is None:
            raise SystemExit("build manifest has invalid Render Core source identity")
        provenance["renderCoreSourceIdentity"] = identity
    if input_count is not None:
        if not isinstance(input_count, int) or isinstance(input_count, bool) or input_count <= 0:
            raise SystemExit("build manifest has invalid Render Core source input count")
        provenance["renderCoreSourceInputCount"] = input_count
    for field, aliases in {
        "idfVersion": ("idfVersion", "espIdfVersion"),
        "toolchain": ("toolchain", "toolchainId"),
        "buildProfile": ("buildProfile", "profileId"),
    }.items():
        value = next((document[name] for name in aliases if isinstance(document, dict) and name in document), None)
        if value is not None:
            if not isinstance(value, str) or not value.strip():
                raise SystemExit(f"build manifest has invalid {field}")
            provenance[field] = value
    return version, abi, provenance


def _idf_build_provenance(build_dir: Path, firmware: Path) -> tuple[str, int, dict[str, object]]:
    generated = build_dir / "esp-idf" / "jellyframe_render_core" / "generated"
    port_path = generated / "jellyframe_render_core_port_provenance.txt"
    source_path = generated / "jellyframe_render_core_source_manifest.json"
    profile_path = generated / "jellyframe_render_core_profile.json"
    description_path = build_dir / "project_description.json"
    cache_path = build_dir / "CMakeCache.txt"
    sdkconfig_path = build_dir / "sdkconfig"
    required = (port_path, source_path, profile_path, description_path, cache_path, sdkconfig_path)
    if not all(path.is_file() for path in required):
        missing = [str(path) for path in required if not path.is_file()]
        raise SystemExit("ESP-IDF build directory is missing provenance files: " + ", ".join(missing))

    try:
        port = dict(line.split("=", 1) for line in port_path.read_text(encoding="utf-8").splitlines() if "=" in line)
        source = json.loads(source_path.read_text(encoding="utf-8"))
        core_profile = json.loads(profile_path.read_text(encoding="utf-8"))
        description = json.loads(description_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
        raise SystemExit("ESP-IDF build provenance files are invalid") from error
    cache: dict[str, str] = {}
    for line in cache_path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"^([^:#]+):[^=]*=(.*)$", line)
        if match:
            cache[match.group(1)] = match.group(2)

    try:
        version = port["package"]
        abi = int(port["abi"])
        identity = port["source"]
        count = int(port["files"])
        source_identity = source["sourceHash"]
        source_count = source["sourceFileCount"]
        core_version = core_profile["packageVersion"]
        core_abi = core_profile["engineAbi"]
        core_profile_id = core_profile["profileId"]
        idf_revision = description["git_revision"]
        idf_path = Path(description["idf_path"])
        app_bin = description["app_bin"]
        target = description["target"]
        compiler = description["c_compiler"]
        sdkconfig_target = next(line.split("=", 1)[1].strip().strip('"') for line in sdkconfig_path.read_text(encoding="utf-8").splitlines() if line.startswith("CONFIG_IDF_TARGET="))
        idf_cache_target = cache["IDF_TARGET"]
        config_defaults = cache["SDKCONFIG_DEFAULTS"]
    except (KeyError, TypeError, ValueError, StopIteration, OSError) as error:
        raise SystemExit("ESP-IDF build provenance is missing required identity fields") from error
    if (identity != source_identity or count != source_count or version != core_version or abi != core_abi or
            description.get("build_dir") is None or
            Path(description["build_dir"]).resolve() != build_dir.resolve() or
            target != idf_cache_target or target != sdkconfig_target):
        raise SystemExit("ESP-IDF build provenance identities disagree")
    if (not isinstance(identity, str) or re.fullmatch(r"[0-9a-f]{64}", identity) is None or
            count <= 0 or abi <= 0 or not isinstance(idf_revision, str) or not idf_revision or
            not isinstance(compiler, str) or not compiler):
        raise SystemExit("ESP-IDF build provenance has invalid identity values")

    if not isinstance(app_bin, str) or not app_bin.strip():
        raise SystemExit("ESP-IDF build provenance has invalid app BIN path")
    app_bin_path = Path(app_bin)
    if not app_bin_path.is_absolute():
        app_bin_path = build_dir / app_bin_path
    try:
        app_bin_path = app_bin_path.resolve(strict=True)
    except OSError as error:
        raise SystemExit("ESP-IDF build provenance app BIN is missing") from error
    if not app_bin_path.is_file() or not app_bin_path.is_relative_to(build_dir.resolve()):
        raise SystemExit("ESP-IDF build provenance app BIN must be inside the build directory")
    expected_firmware_hash = sha256(app_bin_path)
    if expected_firmware_hash != sha256(firmware):
        raise SystemExit("firmware does not match the app BIN recorded by the ESP-IDF build")

    version_path = idf_path / "tools" / "cmake" / "version.cmake"
    try:
        version_text = version_path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise SystemExit("ESP-IDF version.cmake is missing or invalid") from error
    version_parts: dict[str, str] = {}
    for name in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(rf"(?m)^\s*set\(IDF_VERSION_{name}\s+([0-9]+)\s*\)", version_text)
        if match is None:
            raise SystemExit("ESP-IDF version.cmake has incomplete version metadata")
        version_parts[name] = match.group(1)
    idf_version = ".".join(version_parts[name] for name in ("MAJOR", "MINOR", "PATCH"))

    versioned_toolchain_match = re.search(r"(?:^|[\\/])(esp-[0-9.]+_[0-9]+)(?:[\\/]|$)", compiler)
    versioned_toolchain = versioned_toolchain_match.group(1) if versioned_toolchain_match else None
    if versioned_toolchain is None:
        raise SystemExit("ESP-IDF build provenance does not identify a versioned compiler toolchain")
    defaults = [Path(value).stem.removeprefix("sdkconfig.").removesuffix(".defaults")
                for value in config_defaults.split(";") if Path(value).name.endswith(".defaults")]
    named_profiles = [value for value in defaults if value.startswith("ws147_")]
    if not named_profiles:
        raise SystemExit("ESP-IDF build provenance does not identify a named WS147 profile")

    provenance: dict[str, object] = {
        "renderCoreSourceIdentity": identity,
        "renderCoreSourceInputCount": count,
        "idfVersion": idf_version,
        "idfRevision": idf_revision,
        "toolchain": versioned_toolchain,
        "target": target,
        "buildProfile": ",".join(named_profiles) + ";" + core_profile_id,
        "sdkconfigSha256": sha256(sdkconfig_path),
    }
    return version, abi, provenance


def resolve_render_core(args: argparse.Namespace) -> tuple[str, int, dict[str, object]]:
    explicit = args.render_core_version is not None or args.render_core_abi is not None
    if explicit and (args.render_core_version is None or args.render_core_abi is None):
        raise SystemExit("--render-core-version and --render-core-abi must be supplied together")
    if explicit:
        version = args.render_core_version
        abi = args.render_core_abi
        if VERSION_PATTERN.fullmatch(version) is None or abi <= 0:
            raise SystemExit("explicit Render Core version/ABI is invalid")
        selected = (version, abi)
    else:
        selected = (DEFAULT_RENDER_CORE_VERSION, DEFAULT_RENDER_CORE_ABI)
    provenance: dict[str, object] = {}

    if args.build_manifest is not None:
        from_manifest = _render_core_from_build_manifest(args.build_manifest.resolve())
        if explicit and selected != from_manifest[:2]:
            raise SystemExit("explicit Render Core metadata conflicts with build manifest")
        selected = from_manifest[:2]
        provenance = from_manifest[2]
    return selected[0], selected[1], provenance


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--factory-image", required=True, type=Path)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--image-version", required=True)
    parser.add_argument("--build-manifest", type=Path,
                        help="firmware build manifest with sources.renderCore packageVersion/engineAbi and optional provenance fields")
    parser.add_argument("--build-dir", type=Path,
                        help="ESP-IDF build directory containing generated Core provenance, project_description.json, CMakeCache.txt and sdkconfig")
    parser.add_argument("--render-core-version")
    parser.add_argument("--render-core-abi", type=int)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    if args.build_manifest is not None and args.build_dir is not None:
        raise SystemExit("--build-manifest and --build-dir are mutually exclusive")

    firmware = args.firmware.resolve()
    factory = args.factory_image.resolve()
    if not firmware.is_file() or not factory.is_file():
        raise SystemExit("firmware and factory image must exist")
    if len(args.source_revision) != 40 or any(char not in "0123456789abcdef" for char in args.source_revision):
        raise SystemExit("source revision must be 40 lowercase hexadecimal characters")
    if factory.stat().st_size != 16 * 1024 * 1024:
        raise SystemExit("factory image must be a complete 16 MiB flash image")
    if args.build_dir is not None:
        render_core_version, render_core_abi, build_provenance = _idf_build_provenance(
            args.build_dir.resolve(), firmware)
        explicit = (args.render_core_version, args.render_core_abi)
        if any(value is not None for value in explicit) and explicit != (render_core_version, render_core_abi):
            raise SystemExit("explicit Render Core metadata conflicts with ESP-IDF build directory")
    else:
        render_core_version, render_core_abi, build_provenance = resolve_render_core(args)

    output = args.output.resolve()
    archive_name = f"jellyframe-ws147-developer-{args.image_version}-provider-{PROVIDER_VERSION}"
    release = output / archive_name
    if release.exists():
        raise SystemExit(f"output already exists: {release}")

    manifest = {
        "format": "jellyframe.device-image", "formatVersion": 0,
        "imageId": "org.jellyframe.ws147.developer", "imageVersion": args.image_version,
        "runtimeVersion": "0.6.0-dev",
        "renderCore": {"version": render_core_version, "abi": render_core_abi},
        "source": {"revision": args.source_revision, "firmwareSha256": sha256(firmware)},
        "board": {"id": "ws147", "display": {"width": 172, "height": 320, "shape": "rect"}},
        "profile": {"id": "rect-172x320", "featureFamilies": FEATURE_FAMILIES},
        "transport": {"protocol": "JFDP/1", "kind": "usb-serial-jtag"},
        "storage": {"maxBundleBytes": 327680},
        "recovery": {"procedureId": "ws147-usb-recovery-v1", "factoryImageSha256": sha256(factory)},
    }
    if build_provenance:
        manifest["buildProvenance"] = build_provenance

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
