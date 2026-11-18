#!/usr/bin/env python3
"""Create a self-contained WS147 Developer Image and provider archive."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from provider_version import PROVIDER_VERSION


ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))
from device_image_manifest import DeviceImageManifestError, parse_device_image_manifest  # noqa: E402
from device_render_core_capabilities import KNOWN_FEATURE_FAMILIES, supported_text_pipelines

PROVIDER_SOURCE = Path(__file__).resolve().parent
PROVIDER_FEATURE_FAMILIES = KNOWN_FEATURE_FAMILIES
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


def _identity_from_build_evidence(build_manifest_path: Path, profile_path: Path, firmware: Path,
                                  build_profile: str) -> tuple[str, str, str, int, list[str], dict[str, object]]:
    try:
        document = json.loads(build_manifest_path.read_text(encoding="utf-8"))
        profile = json.loads(profile_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise SystemExit("build manifest and Core profile must be valid UTF-8 JSON") from error

    try:
        schema_version = document["schemaVersion"]
        target = document["target"]
        board = document["board"]
        revision = document["runtime"]["buildCommit"]
        selected_profile = document["profiles"][build_profile]
        image_version = selected_profile["imageVersion"]
        binary = selected_profile["bin"]
        expected_firmware_hash = binary["sha256"]
        version = document["renderCore"]["version"]
        abi = document["renderCore"]["engineAbi"]
        profile_schema = profile["schemaVersion"]
        profile_version = profile["packageVersion"]
        profile_abi = profile["engineAbi"]
        profile_features = profile["features"]
    except (KeyError, TypeError, AttributeError) as error:
        raise SystemExit(f"build manifest or Core profile is missing required identity fields for {build_profile}") from error

    if type(schema_version) is not int or schema_version != 1 or target != "esp32s3" or board != "Waveshare ESP32-S3-Touch-LCD-1.47":
        raise SystemExit("build manifest must describe the WS147 ESP32-S3 target")
    if type(profile_schema) is not int or profile_schema != 1:
        raise SystemExit("generated Render Core profile has an unsupported schema version")
    if not isinstance(revision, str) or re.fullmatch(r"[0-9a-f]{40}", revision) is None:
        raise SystemExit("build manifest runtime.buildCommit must be 40 lowercase hexadecimal characters")
    if not isinstance(image_version, str) or VERSION_PATTERN.fullmatch(image_version) is None:
        raise SystemExit(f"build manifest profile {build_profile}.imageVersion is invalid")
    if not isinstance(version, str) or VERSION_PATTERN.fullmatch(version) is None:
        raise SystemExit("build manifest has an invalid Render Core version")
    if not isinstance(abi, int) or isinstance(abi, bool) or abi <= 0:
        raise SystemExit("build manifest has an invalid Render Core engineAbi")
    if (not isinstance(profile_abi, int) or isinstance(profile_abi, bool) or
            profile_version != version or profile_abi != abi):
        raise SystemExit("generated Render Core profile identity does not match the build manifest")
    if (not isinstance(profile_features, list) or not profile_features or
            any(not isinstance(item, str) for item in profile_features) or
            len(set(profile_features)) != len(profile_features)):
        raise SystemExit("generated Render Core profile features must be a non-empty unique string array")
    unsupported = set(profile_features) - PROVIDER_FEATURE_FAMILIES
    if unsupported:
        raise SystemExit("generated Render Core profile contains feature families the JFDP provider cannot encode: " +
                         ", ".join(sorted(unsupported)))
    try:
        supported_text_pipelines(abi, profile_features)
    except ValueError as error:
        raise SystemExit(f"generated Render Core profile has conflicting text capabilities: {error}") from error
    if (not isinstance(expected_firmware_hash, str) or
            re.fullmatch(r"[0-9a-fA-F]{64}", expected_firmware_hash) is None):
        raise SystemExit(f"build manifest profile {build_profile}.bin.sha256 is invalid")
    expected_firmware_hash = expected_firmware_hash.lower()
    if not isinstance(binary.get("path"), str) or not binary["path"]:
        raise SystemExit(f"build manifest profile {build_profile}.bin.path must be non-empty")
    binary_path = Path(binary["path"])
    if binary_path.is_absolute():
        raise SystemExit(f"build manifest profile {build_profile}.bin.path must be relative")
    evidence_root = build_manifest_path.parent.resolve()
    evidenced_firmware = (evidence_root / binary_path).resolve()
    if not evidenced_firmware.is_relative_to(evidence_root):
        raise SystemExit(f"build manifest profile {build_profile}.bin.path escapes the evidence directory")
    if not evidenced_firmware.is_file() or sha256(evidenced_firmware) != expected_firmware_hash:
        raise SystemExit("build manifest firmware path does not match its recorded SHA-256")
    if sha256(firmware) != expected_firmware_hash:
        raise SystemExit(f"firmware does not match the {build_profile} binary in the build manifest")

    provenance: dict[str, object] = {}
    core = document["renderCore"]
    identity = core.get("sourceHashSha256")
    input_count = core.get("sourceFileCount")
    if identity is not None:
        if not isinstance(identity, str) or re.fullmatch(r"[0-9a-f]{64}", identity) is None:
            raise SystemExit("build manifest has invalid Render Core sourceHashSha256")
        provenance["renderCoreSourceIdentity"] = identity
    if input_count is not None:
        if not isinstance(input_count, int) or isinstance(input_count, bool) or input_count <= 0:
            raise SystemExit("build manifest has invalid Render Core source input count")
        provenance["renderCoreSourceInputCount"] = input_count
    for field, section, key in (
        ("idfVersion", "espIdf", "version"),
        ("toolchain", "toolchain", "xtensaCompiler"),
    ):
        value = document.get(section, {}).get(key) if isinstance(document.get(section), dict) else None
        if value is not None:
            if not isinstance(value, str) or not value.strip():
                raise SystemExit(f"build manifest has invalid {field}")
            provenance[field] = value
    return revision, image_version, version, abi, profile_features, provenance


def _idf_build_provenance(build_dir: Path, firmware: Path) -> tuple[str, int, list[str], dict[str, object]]:
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
        core_features = core_profile["features"]
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
    if (not isinstance(core_features, list) or not core_features or
            any(not isinstance(item, str) for item in core_features) or
            len(set(core_features)) != len(core_features) or
            set(core_features) - PROVIDER_FEATURE_FAMILIES):
        raise SystemExit("ESP-IDF generated Core profile has invalid or unsupported feature families")
    try:
        supported_text_pipelines(abi, core_features)
    except ValueError as error:
        raise SystemExit(f"ESP-IDF Core text capabilities conflict: {error}") from error
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
    return version, abi, core_features, provenance


def _install_capacity(build_dir: Path, firmware: Path, factory: Path | None = None) -> tuple[int, dict[str, object]]:
    """Mirror DeviceImageStore's 2 registry sectors + 3 aligned bundle slots.

    Use generated binary partitions, never the editable CSV or profile name.
    Recovery images must contain these same partitions and exact application.
    """
    try:
        values = {}
        for line in (build_dir / "sdkconfig").read_text(encoding="utf-8").splitlines():
            if line.startswith("CONFIG_") and "=" in line:
                key, value = line.split("=", 1)
                # IDF's generated compatibility aliases can repeat identical
                # assignments. Conflicting values remain ambiguous evidence.
                if key in values and values[key] != value:
                    raise ValueError("conflicting sdkconfig key")
                values[key] = value
        configured = int(values["CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB"]) * 1024
        table_at = int(values["CONFIG_PARTITION_TABLE_OFFSET"], 0)
        generated_config = (build_dir / "config" / "sdkconfig.h").read_text(encoding="utf-8")
        for key in ("CONFIG_PARTITION_TABLE_OFFSET", "CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB"):
            definitions = re.findall(rf"(?m)^#define {key}\s+(\S+)\s*$", generated_config)
            if len(definitions) != 1 or int(definitions[0], 0) != int(values[key], 0):
                raise ValueError("generated sdkconfig header disagrees with capacity configuration")
        table = (build_dir / "partition_table" / "partition-table.bin").read_bytes()
        if (not 64 * 1024 <= configured <= 2048 * 1024 or
                not 4096 <= table_at <= 16 * 1024 * 1024 - 4096 or table_at % 4096):
            raise ValueError("invalid configured capacity or partition offset")
        if not table or len(table) > 4096 or len(table) % 32:
            raise ValueError("invalid binary partition table length")
        partitions = []
        digest_seen = False
        ended = False
        for at in range(0, len(table), 32):
            record = table[at:at + 32]
            if record == bytes([255]) * 32:
                ended = True
                continue
            if ended:
                raise ValueError("records after partition terminator")
            if record[:16] == b"\xeb\xeb" + bytes([255]) * 14:
                if digest_seen or record[16:] != hashlib.md5(table[:at]).digest():
                    raise ValueError("partition table MD5 mismatch")
                digest_seen = True
                continue
            if digest_seen:
                raise ValueError("partition after MD5")
            magic, kind, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", record)
            if magic != 0x50aa or not size or offset < table_at + 4096 or offset + size > 16 * 1024 * 1024:
                raise ValueError("invalid partition bounds")
            name = label.rstrip(b"\0").decode("ascii")
            if not name or any(p[0] == name or (offset < p[4] + p[3] and p[3] < offset + size) for p in partitions):
                raise ValueError("duplicate or overlapping partition")
            partitions.append((name, kind, subtype, offset, size, flags))
        if not digest_seen:
            raise ValueError("partition MD5 record missing")
        stores = [p for p in partitions if p[0] == "storage" and p[1:3] == (1, 2) and p[5] == 0]
        apps = [p for p in partitions if p[0] == "factory" and p[1:3] == (0, 0) and p[5] == 0]
        if len(stores) != 1 or len(apps) != 1:
            raise ValueError("missing supported storage/factory partition")
        storage, app = stores[0], apps[0]
        if storage[3] % 4096 or storage[4] % 4096 or storage[4] <= 8192:
            raise ValueError("invalid storage alignment/size")
        effective = min(configured, ((storage[4] - 8192) // 3) & ~4095)
        if not effective or firmware.stat().st_size > app[4]:
            raise ValueError("no install slot or application exceeds partition")
        if factory is not None:
            with factory.open("rb") as image:
                image.seek(table_at)
                if image.read(len(table)) != table:
                    raise ValueError("factory image partition table does not match build")
                image.seek(app[3])
                if image.read(firmware.stat().st_size) != firmware.read_bytes():
                    raise ValueError("factory image application does not match firmware")
        return effective, {
            "partitionTableSha256": hashlib.sha256(table).hexdigest(),
            "configuredMaxBundleBytes": configured,
            "storagePartitionOffset": storage[3], "storagePartitionBytes": storage[4],
        }
    except (OSError, UnicodeError, ValueError, KeyError, struct.error) as error:
        raise SystemExit(f"invalid ESP-IDF install capacity evidence: {error}") from error


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--factory-image", required=True, type=Path)
    parser.add_argument("--build-manifest", required=True, type=Path,
                        help="build manifest with installedWorker firmware, image/source, and Render Core identity")
    parser.add_argument("--render-core-profile", required=True, type=Path,
                        help="generated jellyframe_render_core_profile.json from this build")
    parser.add_argument("--build-profile", required=True,
                        help="named profile in the build manifest, such as native or installedWorker")
    parser.add_argument("--build-dir", type=Path,
                        help="optional ESP-IDF build directory for additional provenance cross-checks")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    firmware = args.firmware.resolve()
    factory = args.factory_image.resolve()
    if not firmware.is_file() or not factory.is_file():
        raise SystemExit("firmware and factory image must exist")
    if factory.stat().st_size != 16 * 1024 * 1024:
        raise SystemExit("factory image must be a complete 16 MiB flash image")
    (source_revision, image_version, render_core_version, render_core_abi,
     feature_families, build_provenance) = _identity_from_build_evidence(
        args.build_manifest.resolve(), args.render_core_profile.resolve(), firmware, args.build_profile)
    max_bundle_bytes = 327680  # Historical scalar releases without build-dir evidence.
    if any(family.startswith('text.') for family in feature_families) and args.build_dir is None:
        raise SystemExit("text pipeline provider release requires --build-dir capacity evidence")
    if args.build_dir is not None:
        idf_version, idf_abi, idf_features, idf_provenance = _idf_build_provenance(
            args.build_dir.resolve(), firmware)
        if ((render_core_version, render_core_abi) != (idf_version, idf_abi) or
                set(feature_families) != set(idf_features)):
            raise SystemExit("build manifest and supplied Core profile conflict with ESP-IDF build directory")
        idf_revision = idf_provenance.get("idfRevision")
        if re.fullmatch(r"[0-9a-f]{40}", str(idf_revision)) and idf_revision != source_revision:
            raise SystemExit("build manifest source revision conflicts with ESP-IDF build directory")
        for field in ("renderCoreSourceIdentity", "renderCoreSourceInputCount"):
            if field in build_provenance and build_provenance[field] != idf_provenance[field]:
                raise SystemExit("build manifest Core source identity conflicts with ESP-IDF build directory")
        if "idfVersion" in build_provenance:
            recorded_idf = str(build_provenance["idfVersion"]).removeprefix("v").removesuffix("-dirty")
            if recorded_idf != idf_provenance["idfVersion"]:
                raise SystemExit("build manifest IDF version conflicts with ESP-IDF build directory")
        if "toolchain" in build_provenance:
            recorded_toolchain = re.search(r"esp-[0-9.]+_[0-9]+", str(build_provenance["toolchain"]))
            if recorded_toolchain is None or recorded_toolchain.group() != idf_provenance["toolchain"]:
                raise SystemExit("build manifest versioned toolchain conflicts with ESP-IDF build directory")
        max_bundle_bytes, capacity_provenance = _install_capacity(args.build_dir.resolve(), firmware, factory)
        build_provenance.update(idf_provenance)
        build_provenance.update(capacity_provenance)

    output = args.output.resolve()
    archive_name = f"jellyframe-ws147-developer-{image_version}-provider-{PROVIDER_VERSION}"
    release = output / archive_name
    if release.exists():
        raise SystemExit(f"output already exists: {release}")

    manifest = {
        "format": "jellyframe.device-image", "formatVersion": 0,
        "imageId": "org.jellyframe.ws147.developer", "imageVersion": image_version,
        "runtimeVersion": "0.6.0-dev",
        "renderCore": {"version": render_core_version, "abi": render_core_abi},
        "source": {"revision": source_revision, "firmwareSha256": sha256(firmware)},
        "board": {"id": "ws147", "display": {"width": 172, "height": 320, "shape": "rect"}},
        "profile": {"id": "rect-172x320", "featureFamilies": feature_families},
        "transport": {"protocol": "JFDP/1", "kind": "usb-serial-jtag"},
        "storage": {"maxBundleBytes": max_bundle_bytes},
        "recovery": {"procedureId": "ws147-usb-recovery-v1", "factoryImageSha256": sha256(factory)},
    }
    if build_provenance:
        manifest["buildProvenance"] = build_provenance
    try:
        parse_device_image_manifest(json.dumps(manifest))
    except DeviceImageManifestError as error:
        raise SystemExit(f"invalid Developer Image manifest: {error}") from error

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
        for module in ("device_image_manifest.py", "device_install_package.py", "device_render_core_capabilities.py"):
            copy(ROOT / "tools" / module, provider / "lib" / module)
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
