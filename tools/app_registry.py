#!/usr/bin/env python3
import argparse
import datetime as _datetime
import hashlib
import json
import os
import re
import shutil
import struct
import zlib
from pathlib import Path


JFAPP_MAGIC = b"JFAPPV0\0"
JFAPP_HEADER_FORMAT = "<8sHHIIIIIIIIIII"
JFAPP_HEADER_SIZE = struct.calcsize(JFAPP_HEADER_FORMAT)
JFAPP_ENTRY_FORMAT = "<IIHHIIII"
JFAPP_ENTRY_SIZE = 28
REGISTRY_FORMAT = "jellyframe.installed_apps.registry"
REGISTRY_VERSION = 0
INSTALL_CANDIDATE_FORMAT = "jellyframe.install_candidate"
INSTALL_CANDIDATE_VERSION = 0
APP_MANAGER_STATE_FORMAT = "jellyframe.app_manager.state"
APP_MANAGER_STATE_VERSION = 0
APP_STATUS_INSTALLED = "installed"
APP_STATUS_DISABLED = "disabled"
APP_STATUS_FAILED = "failed"
DEFAULT_MAX_APPS = 32
DEFAULT_MAX_BUNDLE_BYTES = 4 * 1024 * 1024
MAX_INSTALL_CANDIDATE_BYTES = 128 * 1024
UPDATE_POLICY_REJECT_DOWNGRADE = "reject-downgrade"


def fail(message: str) -> None:
    raise SystemExit(f"jellyframe_app_registry: {message}")


def utc_now() -> str:
    return _datetime.datetime.now(_datetime.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def registry_path(store: Path) -> Path:
    return store / "registry.json"


def bundles_dir(store: Path) -> Path:
    return store / "bundles"


def staging_dir(store: Path) -> Path:
    return store / "staging"


def data_dir(store: Path) -> Path:
    return store / "data"


def app_data_dir(store: Path, app_id: str) -> Path:
    return data_dir(store) / sanitize_filename(app_id)


def sanitize_filename(value: str) -> str:
    cleaned = re.sub(r"[^a-zA-Z0-9_.-]+", "_", value).strip("._")
    return cleaned or "app"


def read_json(path: Path, max_bytes: int | None = None) -> dict:
    try:
        if max_bytes is not None and path.stat().st_size > max_bytes:
            fail(f"JSON exceeds max bytes: {path}")
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except json.JSONDecodeError as error:
        fail(f"invalid JSON {path}: {error}")
    if not isinstance(value, dict):
        fail(f"JSON root must be an object: {path}")
    return value


def load_install_candidate(path: Path) -> dict:
    candidate = read_json(path, MAX_INSTALL_CANDIDATE_BYTES)
    if candidate.get("format") != INSTALL_CANDIDATE_FORMAT:
        fail(f"install candidate format must be {INSTALL_CANDIDATE_FORMAT}: {path}")
    if int(candidate.get("formatVersion", -1)) != INSTALL_CANDIDATE_VERSION:
        fail(f"unsupported install candidate formatVersion: {path}")
    bundle = candidate.get("bundle", {})
    if not isinstance(bundle, dict):
        fail(f"install candidate bundle must be an object: {path}")
    bundle_path = bundle.get("path")
    if not isinstance(bundle_path, str) or not bundle_path:
        fail(f"install candidate bundle.path is required: {path}")
    resolved_bundle = Path(bundle_path)
    if not resolved_bundle.is_absolute():
        resolved_bundle = path.parent / resolved_bundle
    candidate["bundlePath"] = resolved_bundle
    return candidate


def atomic_write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    os.replace(tmp, path)


def load_registry(store: Path) -> dict:
    path = registry_path(store)
    if not path.is_file():
        return {
            "format": REGISTRY_FORMAT,
            "formatVersion": REGISTRY_VERSION,
            "apps": [],
        }
    registry = read_json(path)
    if registry.get("format") != REGISTRY_FORMAT or registry.get("formatVersion") != REGISTRY_VERSION:
        fail(f"unsupported registry format: {path}")
    apps = registry.get("apps", [])
    if not isinstance(apps, list):
        fail(f"registry apps must be a list: {path}")
    for index, entry in enumerate(apps):
        if not isinstance(entry, dict):
            fail(f"registry apps[{index}] must be an object: {path}")
        bundle_path_in_store(store, entry.get("bundleFile"), f"registry apps[{index}]")
        rollback = entry.get("rollback")
        if rollback is not None:
            if not isinstance(rollback, dict):
                fail(f"registry apps[{index}].rollback must be an object: {path}")
            bundle_path_in_store(store, rollback.get("bundleFile"), f"registry apps[{index}].rollback")
    registry["apps"] = apps
    return registry


def sorted_registry(registry: dict) -> dict:
    registry = dict(registry)
    registry["apps"] = sorted(registry.get("apps", []), key=lambda app: str(app.get("id", "")))
    return registry


def app_manager_state_from_registry(store: Path, registry: dict) -> dict:
    apps = []
    for entry in sorted_registry(registry).get("apps", []):
        rollback = entry.get("rollback")
        failure = entry.get("failure")
        status = entry.get("status", APP_STATUS_INSTALLED)
        enabled = bool(entry.get("enabled", True))
        rollback_ready = isinstance(rollback, dict) and bool(rollback.get("bundleFile"))
        failure_record = failure if isinstance(failure, dict) else {}
        apps.append({
            "id": entry.get("id", ""),
            "name": entry.get("name", entry.get("id", "")),
            "role": entry.get("role", "app"),
            "versionName": entry.get("versionName", ""),
            "versionCode": int(entry.get("versionCode", 0) or 0),
            "status": status,
            "enabled": enabled,
            "launchable": enabled and status == APP_STATUS_INSTALLED,
            "rollbackReady": rollback_ready,
            "bundleSize": int(entry.get("bundleSize", 0) or 0),
            "updatedAtUtc": entry.get("updatedAtUtc", entry.get("installedAtUtc", "")),
            "failure": {
                "reason": failure_record.get("reason", ""),
                "message": failure_record.get("message", ""),
                "failedAtUtc": failure_record.get("failedAtUtc", ""),
            } if failure_record else {},
        })
    return {
        "format": APP_MANAGER_STATE_FORMAT,
        "formatVersion": APP_MANAGER_STATE_VERSION,
        "store": str(store.resolve()),
        "generatedAtUtc": utc_now(),
        "apps": apps,
        "summary": {
            "appCount": len(apps),
            "launchableCount": sum(1 for app in apps if app["launchable"]),
            "failedCount": sum(1 for app in apps if app["status"] == APP_STATUS_FAILED),
            "rollbackReadyCount": sum(1 for app in apps if app["rollbackReady"]),
        },
    }


def read_bundle(path: Path, max_bundle_bytes: int) -> bytes:
    if not path.is_file():
        fail(f"bundle does not exist: {path}")
    size = path.stat().st_size
    if max_bundle_bytes > 0 and size > max_bundle_bytes:
        fail(f"bundle exceeds max bytes: {path} ({size} > {max_bundle_bytes})")
    return path.read_bytes()


def byte_range_is_valid(total: int, offset: int, size: int) -> bool:
    return 0 <= offset <= total and 0 <= size <= total - offset


def bundle_path_in_store(store: Path, bundle_file: object, context: str, require_exists: bool = False) -> Path:
    if not isinstance(bundle_file, str) or not bundle_file:
        fail(f"{context} bundle file is required")
    filename = Path(bundle_file)
    if filename.name != bundle_file or filename.suffix != ".jfapp":
        fail(f"{context} bundle file must be a .jfapp basename")

    root = bundles_dir(store.resolve())
    path = root / filename
    root_resolved = root.resolve(strict=False)
    path_resolved = path.resolve(strict=False)
    try:
        path_resolved.relative_to(root_resolved)
    except ValueError:
        fail(f"{context} bundle file escapes the bundle store")
    if path.is_symlink():
        fail(f"{context} bundle file must not be a symlink")
    if require_exists and not path.is_file():
        fail(f"{context} bundle is missing: {path}")
    return path


def parse_jfapp(bundle: bytes) -> dict:
    if len(bundle) < JFAPP_HEADER_SIZE:
        fail("bundle is too small to contain a .jfapp header")
    (
        magic,
        header_size,
        format_version,
        flags,
        summary_offset,
        summary_size,
        index_offset,
        resource_count,
        string_table_offset,
        string_table_size,
        payload_offset,
        payload_size,
        expected_crc32,
        reserved,
    ) = struct.unpack_from(JFAPP_HEADER_FORMAT, bundle, 0)
    if magic != JFAPP_MAGIC:
        fail("bundle magic is not JFAPPV0")
    if header_size != JFAPP_HEADER_SIZE or format_version != 0:
        fail("unsupported .jfapp format version")
    if flags != 0 or reserved != 0:
        fail(".jfapp V0 flags/reserved fields must be zero")
    if not byte_range_is_valid(len(bundle), summary_offset, summary_size):
        fail(".jfapp summary section is out of range")
    if not byte_range_is_valid(len(bundle), index_offset, resource_count * JFAPP_ENTRY_SIZE):
        fail(".jfapp resource index is out of range")
    if not byte_range_is_valid(len(bundle), string_table_offset, string_table_size):
        fail(".jfapp string table is out of range")
    if not byte_range_is_valid(len(bundle), payload_offset, payload_size):
        fail(".jfapp payload section is out of range")
    if expected_crc32 != 0:
        crc_bytes = bytearray(bundle)
        struct.pack_into("<I", crc_bytes, 48, 0)
        actual_crc32 = zlib.crc32(crc_bytes) & 0xffffffff
        if actual_crc32 != expected_crc32:
            fail(f".jfapp checksum mismatch: {actual_crc32:08x} != {expected_crc32:08x}")
    for index in range(resource_count):
        entry_offset = index_offset + index * JFAPP_ENTRY_SIZE
        (
            _path_hash,
            path_offset,
            path_size,
            _kind,
            entry_payload_offset,
            entry_payload_size,
            _entry_crc32,
            _flags,
        ) = struct.unpack_from(JFAPP_ENTRY_FORMAT, bundle, entry_offset)
        if not byte_range_is_valid(string_table_size, path_offset, path_size):
            fail(f".jfapp resource index entry {index} path is out of range")
        if not byte_range_is_valid(payload_size, entry_payload_offset, entry_payload_size):
            fail(f".jfapp resource index entry {index} payload is out of range")
    summary_text = bundle[summary_offset:summary_offset + summary_size].decode("utf-8")
    try:
        summary = json.loads(summary_text)
    except json.JSONDecodeError as error:
        fail(f".jfapp summary JSON is invalid: {error}")
    app_id = summary.get("id")
    if not isinstance(app_id, str) or not app_id:
        fail(".jfapp summary is missing app id")
    return {
        "summary": summary,
        "resourceCount": resource_count,
        "payloadBytes": payload_size,
        "crc32": f"{expected_crc32:08x}",
        "sha256": hashlib.sha256(bundle).hexdigest(),
        "size": len(bundle),
    }


def bundle_filename(summary: dict, sha256: str) -> str:
    app_id = sanitize_filename(str(summary.get("id", "app")))
    version_code = int(summary.get("versionCode", 0) or 0)
    return f"{app_id}-{version_code}-{sha256[:12]}.jfapp"


def make_registry_entry(bundle_info: dict, bundle_file: str) -> dict:
    summary = bundle_info["summary"]
    now = utc_now()
    return {
        "id": summary["id"],
        "name": summary.get("name", summary["id"]),
        "role": summary.get("role", "app"),
        "status": APP_STATUS_INSTALLED,
        "enabled": True,
        "versionName": summary.get("versionName", "0.0.0"),
        "versionCode": int(summary.get("versionCode", 0) or 0),
        "entry": summary.get("entry", "/index.html"),
        "minJellyFrame": summary.get("minJellyFrame", ""),
        "script": summary.get("script", "classic"),
        "networkAllowed": bool(summary.get("networkAllowed", False)),
        "bundleFile": bundle_file,
        "bundleSize": bundle_info["size"],
        "bundleCrc32": bundle_info["crc32"],
        "bundleSha256": bundle_info["sha256"],
        "resourceCount": bundle_info["resourceCount"],
        "payloadBytes": bundle_info["payloadBytes"],
        "installedAtUtc": now,
        "updatedAtUtc": now,
    }


def rollback_record_from_entry(entry: dict) -> dict:
    keys = [
        "id",
        "name",
        "role",
        "versionName",
        "versionCode",
        "entry",
        "minJellyFrame",
        "script",
        "networkAllowed",
        "bundleFile",
        "bundleSize",
        "bundleCrc32",
        "bundleSha256",
        "resourceCount",
        "payloadBytes",
        "installedAtUtc",
        "updatedAtUtc",
    ]
    return {key: entry[key] for key in keys if key in entry}


def update_policy_decision(previous: dict | None, summary: dict, allow_downgrade: bool = False) -> dict:
    incoming_code = int(summary.get("versionCode", 0) or 0)
    previous_code = int(previous.get("versionCode", 0) or 0) if previous else 0
    action = "install" if previous is None else ("reinstall" if previous_code == incoming_code else "update")
    downgrade = previous is not None and incoming_code < previous_code
    allowed = not downgrade or allow_downgrade
    reason = "" if allowed else "downgrade-blocked"
    return {
        "policy": UPDATE_POLICY_REJECT_DOWNGRADE,
        "allowed": allowed,
        "allowDowngrade": allow_downgrade,
        "action": action,
        "reason": reason,
        "previousVersionCode": previous_code,
        "incomingVersionCode": incoming_code,
    }


def apply_rollback_record(base_entry: dict, rollback: dict) -> dict:
    restored = dict(base_entry)
    current_record = rollback_record_from_entry(base_entry)
    for key, value in rollback.items():
        restored[key] = value
    restored["status"] = APP_STATUS_INSTALLED
    restored["enabled"] = True
    restored["updatedAtUtc"] = utc_now()
    restored["rollback"] = current_record
    return restored


def install_bundle(
    store: Path,
    bundle_path: Path,
    max_apps: int,
    max_bundle_bytes: int,
    allow_downgrade: bool = False,
) -> dict:
    store = store.resolve()
    bundle_path = bundle_path.resolve()
    bundle = read_bundle(bundle_path, max_bundle_bytes)
    bundle_info = parse_jfapp(bundle)
    registry = load_registry(store)
    apps = registry["apps"]
    app_id = bundle_info["summary"]["id"]
    old_entry = next((app for app in apps if app.get("id") == app_id), None)
    if old_entry is None and len(apps) >= max_apps:
        fail(f"registry is full: {len(apps)} >= {max_apps}")
    decision = update_policy_decision(old_entry, bundle_info["summary"], allow_downgrade)
    if not decision["allowed"]:
        fail(
            "downgrade install is blocked: "
            f"{app_id} {decision['incomingVersionCode']} < {decision['previousVersionCode']}; "
            "use --allow-downgrade or rollback"
        )

    final_name = bundle_filename(bundle_info["summary"], bundle_info["sha256"])
    final_path = bundle_path_in_store(store, final_name, "generated")
    stage_path = staging_dir(store) / (final_name + ".staging")
    staging_dir(store).mkdir(parents=True, exist_ok=True)
    bundles_dir(store).mkdir(parents=True, exist_ok=True)
    try:
        shutil.copyfile(bundle_path, stage_path)
        staged = stage_path.read_bytes()
        if hashlib.sha256(staged).hexdigest() != bundle_info["sha256"]:
            fail("staged bundle hash changed during copy")
        os.replace(stage_path, final_path)
        entry = make_registry_entry(bundle_info, final_name)
        obsolete_rollback_file = None
        if old_entry is None:
            apps.append(entry)
        else:
            old_file = old_entry.get("bundleFile")
            old_rollback = old_entry.get("rollback", {})
            if isinstance(old_rollback, dict):
                rollback_file = old_rollback.get("bundleFile")
                if isinstance(rollback_file, str) and rollback_file and rollback_file not in {old_file, final_name}:
                    obsolete_rollback_file = rollback_file
            entry["installedAtUtc"] = old_entry.get("installedAtUtc", entry["installedAtUtc"])
            entry["enabled"] = bool(old_entry.get("enabled", entry["enabled"]))
            entry["status"] = old_entry.get("status", entry["status"])
            if isinstance(old_file, str) and old_file and old_file != final_name:
                entry["rollback"] = rollback_record_from_entry(old_entry)
            elif isinstance(old_rollback, dict) and old_rollback:
                entry["rollback"] = old_rollback
            apps[apps.index(old_entry)] = entry
        atomic_write_json(registry_path(store), sorted_registry(registry))
    finally:
        if stage_path.exists():
            stage_path.unlink()

    if old_entry is not None and obsolete_rollback_file:
        obsolete_path = bundle_path_in_store(store, obsolete_rollback_file, "rollback")
        if obsolete_path.exists():
            obsolete_path.unlink()
    return entry


def existing_app_entry(store: Path, app_id: str) -> dict | None:
    registry = load_registry(store.resolve())
    return next((app for app in registry.get("apps", []) if app.get("id") == app_id), None)


def install_action(previous: dict | None, entry: dict) -> str:
    if previous is None:
        return "install"
    if previous.get("bundleFile") == entry.get("bundleFile"):
        return "reinstall"
    return "update"


def build_install_transaction_report(
    store: Path,
    bundle_path: Path,
    entry: dict,
    previous: dict | None,
    source_kind: str = "bundle",
    preflight_report: str = "",
    allow_downgrade: bool = False,
) -> dict:
    rollback = entry.get("rollback")
    rollback_available = isinstance(rollback, dict) and bool(rollback.get("bundleFile"))
    policy = update_policy_decision(previous, entry, allow_downgrade)
    return {
        "format": "jellyframe.install.transaction",
        "formatVersion": 0,
        "source": {
            "kind": source_kind,
            "bundle": str(bundle_path),
            "preflightReport": preflight_report,
        },
        "store": str(store.resolve()),
        "action": install_action(previous, entry),
        "result": "ok",
        "app": {
            "id": entry.get("id", ""),
            "name": entry.get("name", ""),
            "role": entry.get("role", "app"),
            "status": entry.get("status", APP_STATUS_INSTALLED),
            "enabled": bool(entry.get("enabled", True)),
            "versionName": entry.get("versionName", ""),
            "versionCode": int(entry.get("versionCode", 0) or 0),
            "entry": entry.get("entry", "/index.html"),
            "script": entry.get("script", "classic"),
        },
        "previous": {
            "installed": previous is not None,
            "versionName": previous.get("versionName", "") if previous else "",
            "versionCode": int(previous.get("versionCode", 0) or 0) if previous else 0,
            "bundleFile": previous.get("bundleFile", "") if previous else "",
        },
        "bundle": {
            "file": entry.get("bundleFile", ""),
            "size": int(entry.get("bundleSize", 0) or 0),
            "crc32": entry.get("bundleCrc32", ""),
            "sha256": entry.get("bundleSha256", ""),
            "resourceCount": int(entry.get("resourceCount", 0) or 0),
            "payloadBytes": int(entry.get("payloadBytes", 0) or 0),
            "integrity": "validated-header-ranges-crc32-sha256",
        },
        "rollback": {
            "available": rollback_available,
            "versionName": rollback.get("versionName", "") if rollback_available else "",
            "versionCode": int(rollback.get("versionCode", 0) or 0) if rollback_available else 0,
            "bundleFile": rollback.get("bundleFile", "") if rollback_available else "",
        },
        "updatePolicy": policy,
        "dataPolicy": {
            "appPrivateDataTouched": False,
            "appPrivateDataPolicy": "retained",
            "note": "Install, update and rollback preserve app-private data; remove/delete-data commands own data deletion.",
        },
        "transaction": {
            "staging": "bundle-copy-then-atomic-replace",
            "registryCommit": "atomic-json-replace",
            "rollbackBundleRetained": rollback_available,
        },
    }


def build_failed_install_transaction_report(
    store: Path,
    bundle_path: Path,
    bundle_info: dict,
    previous: dict | None,
    reason: str,
    source_kind: str = "bundle",
    preflight_report: str = "",
    allow_downgrade: bool = False,
) -> dict:
    summary = bundle_info["summary"]
    return {
        "format": "jellyframe.install.transaction",
        "formatVersion": 0,
        "source": {
            "kind": source_kind,
            "bundle": str(bundle_path),
            "preflightReport": preflight_report,
        },
        "store": str(store.resolve()),
        "action": update_policy_decision(previous, summary, allow_downgrade)["action"],
        "result": "failed",
        "failure": {
            "reason": reason,
            "message": (
                f"incoming versionCode {int(summary.get('versionCode', 0) or 0)} is lower than "
                f"installed versionCode {int(previous.get('versionCode', 0) or 0) if previous else 0}"
                if reason == "downgrade-blocked" else reason
            ),
        },
        "app": {
            "id": summary.get("id", ""),
            "name": summary.get("name", summary.get("id", "")),
            "role": summary.get("role", "app"),
            "versionName": summary.get("versionName", ""),
            "versionCode": int(summary.get("versionCode", 0) or 0),
            "entry": summary.get("entry", "/index.html"),
            "script": summary.get("script", "classic"),
        },
        "previous": {
            "installed": previous is not None,
            "versionName": previous.get("versionName", "") if previous else "",
            "versionCode": int(previous.get("versionCode", 0) or 0) if previous else 0,
            "bundleFile": previous.get("bundleFile", "") if previous else "",
        },
        "bundle": {
            "size": int(bundle_info.get("size", 0) or 0),
            "crc32": bundle_info.get("crc32", ""),
            "sha256": bundle_info.get("sha256", ""),
            "resourceCount": int(bundle_info.get("resourceCount", 0) or 0),
            "payloadBytes": int(bundle_info.get("payloadBytes", 0) or 0),
            "integrity": "validated-header-ranges-crc32-sha256",
        },
        "rollback": {
            "available": isinstance(previous, dict) and bool(previous.get("bundleFile")),
            "versionName": previous.get("versionName", "") if previous else "",
            "versionCode": int(previous.get("versionCode", 0) or 0) if previous else 0,
            "bundleFile": previous.get("bundleFile", "") if previous else "",
        },
        "updatePolicy": update_policy_decision(previous, summary, allow_downgrade),
        "dataPolicy": {
            "appPrivateDataTouched": False,
            "appPrivateDataPolicy": "retained",
        },
        "transaction": {
            "staging": "not-started",
            "registryCommit": "not-started",
            "rollbackBundleRetained": isinstance(previous, dict) and bool(previous.get("bundleFile")),
        },
    }


def candidate_signature_status(candidate: dict) -> str:
    signature = candidate.get("signature", {})
    if not isinstance(signature, dict):
        return ""
    status = signature.get("status", "")
    return status if isinstance(status, str) else ""


def validate_install_candidate(
    store: Path,
    candidate_path: Path,
    max_bundle_bytes: int,
    allow_untrusted: bool = False,
    allow_downgrade: bool = False,
) -> tuple[Path, bytes, dict, dict | None, dict]:
    candidate = load_install_candidate(candidate_path)
    bundle_path = candidate["bundlePath"]
    bundle = read_bundle(bundle_path, max_bundle_bytes)
    bundle_info = parse_jfapp(bundle)
    expected_sha256 = candidate.get("bundle", {}).get("sha256")
    if not isinstance(expected_sha256, str) or re.fullmatch(r"[0-9a-fA-F]{64}", expected_sha256) is None:
        fail("install candidate bundle.sha256 must be a 64-character hexadecimal SHA-256")
    if expected_sha256.lower() != bundle_info["sha256"]:
        fail("bundle-hash-mismatch: install candidate bundle sha256 mismatch")
    signature_status = candidate_signature_status(candidate)
    if signature_status != "trusted" and not allow_untrusted:
        fail("signature-not-trusted: install candidate signature is not trusted")
    if candidate.get("userApproval") is not True:
        fail("user-approval-required: install candidate requires user approval")
    previous = existing_app_entry(store, bundle_info["summary"]["id"])
    decision = update_policy_decision(previous, bundle_info["summary"], allow_downgrade)
    if not decision["allowed"]:
        fail(
            "downgrade install is blocked: "
            f"{bundle_info['summary']['id']} {decision['incomingVersionCode']} < "
            f"{decision['previousVersionCode']}; use --allow-downgrade or rollback"
        )
    return bundle_path, bundle, bundle_info, previous, candidate


def delete_app_data(store: Path, app_id: str) -> bool:
    store = store.resolve()
    path = app_data_dir(store, app_id)
    if not path.exists():
        return False
    if not path.is_dir():
        fail(f"app data path is not a directory: {path}")
    shutil.rmtree(path)
    return True


def remove_app(store: Path, app_id: str, delete_data: bool = True) -> dict:
    store = store.resolve()
    registry = load_registry(store)
    apps = registry["apps"]
    entry = next((app for app in apps if app.get("id") == app_id), None)
    if entry is None:
        fail(f"app is not installed: {app_id}")
    registry["apps"] = [app for app in apps if app.get("id") != app_id]
    atomic_write_json(registry_path(store), sorted_registry(registry))
    bundle_file = entry.get("bundleFile")
    if bundle_file:
        path = bundle_path_in_store(store, bundle_file, "installed app")
        if path.exists():
            path.unlink()
    rollback = entry.get("rollback", {})
    if isinstance(rollback, dict):
        rollback_file = rollback.get("bundleFile")
        if rollback_file and rollback_file != bundle_file:
            rollback_path = bundle_path_in_store(store, rollback_file, "rollback")
            if rollback_path.exists():
                rollback_path.unlink()
    entry["dataDeleted"] = delete_app_data(store, app_id) if delete_data else False
    entry["dataRetained"] = not delete_data
    return entry


def rollback_app(store: Path, app_id: str) -> dict:
    store = store.resolve()
    registry = load_registry(store)
    apps = registry["apps"]
    entry = next((app for app in apps if app.get("id") == app_id), None)
    if entry is None:
        fail(f"app is not installed: {app_id}")
    rollback = entry.get("rollback")
    if not isinstance(rollback, dict) or not rollback.get("bundleFile"):
        fail(f"app has no rollback bundle: {app_id}")
    rollback_file = rollback.get("bundleFile")
    rollback_path = bundle_path_in_store(store, rollback_file, "rollback", require_exists=True)
    restored = apply_rollback_record(entry, rollback)
    apps[apps.index(entry)] = restored
    atomic_write_json(registry_path(store), sorted_registry(registry))
    return restored


def set_app_enabled(store: Path, app_id: str, enabled: bool) -> dict:
    store = store.resolve()
    registry = load_registry(store)
    apps = registry["apps"]
    entry = next((app for app in apps if app.get("id") == app_id), None)
    if entry is None:
        fail(f"app is not installed: {app_id}")
    entry["enabled"] = enabled
    entry["status"] = APP_STATUS_INSTALLED if enabled else APP_STATUS_DISABLED
    if enabled:
        entry.pop("failure", None)
    entry["updatedAtUtc"] = utc_now()
    atomic_write_json(registry_path(store), sorted_registry(registry))
    return entry


def mark_app_failed(store: Path, app_id: str, reason: str, message: str = "") -> dict:
    store = store.resolve()
    registry = load_registry(store)
    apps = registry["apps"]
    entry = next((app for app in apps if app.get("id") == app_id), None)
    if entry is None:
        fail(f"app is not installed: {app_id}")
    now = utc_now()
    entry["enabled"] = False
    entry["status"] = APP_STATUS_FAILED
    entry["updatedAtUtc"] = now
    entry["failure"] = {
        "reason": reason,
        "message": message,
        "failedAtUtc": now,
    }
    atomic_write_json(registry_path(store), sorted_registry(registry))
    return entry


def find_app(store: Path, app_id: str) -> dict:
    registry = load_registry(store.resolve())
    for app in registry["apps"]:
        if app.get("id") == app_id:
            return app
    fail(f"app is not installed: {app_id}")


def app_bundle_path(store: Path, app_id: str) -> Path:
    app = find_app(store, app_id)
    bundle_file = app.get("bundleFile")
    if not isinstance(bundle_file, str) or not bundle_file:
        fail(f"installed app has no bundle file: {app_id}")
    return bundle_path_in_store(store, bundle_file, "installed app", require_exists=True)


def cmd_install(args: argparse.Namespace) -> int:
    bundle = read_bundle(args.bundle, args.max_bundle_bytes)
    bundle_info = parse_jfapp(bundle)
    previous = existing_app_entry(args.store, bundle_info["summary"]["id"])
    decision = update_policy_decision(previous, bundle_info["summary"], args.allow_downgrade)
    if not decision["allowed"]:
        if args.report:
            atomic_write_json(
                args.report,
                build_failed_install_transaction_report(
                    args.store,
                    args.bundle,
                    bundle_info,
                    previous,
                    decision["reason"],
                    allow_downgrade=args.allow_downgrade,
                ),
            )
        fail(
            "downgrade install is blocked: "
            f"{bundle_info['summary']['id']} {decision['incomingVersionCode']} < "
            f"{decision['previousVersionCode']}; use --allow-downgrade or rollback"
        )
    entry = install_bundle(
        args.store,
        args.bundle,
        args.max_apps,
        args.max_bundle_bytes,
        allow_downgrade=args.allow_downgrade,
    )
    if args.report:
        atomic_write_json(
            args.report,
            build_install_transaction_report(
                args.store,
                args.bundle,
                entry,
                previous,
                allow_downgrade=args.allow_downgrade,
            ),
        )
    if args.json:
        print(json.dumps(entry, ensure_ascii=False, indent=2))
    else:
        print(f"installed {entry['id']} {entry['versionName']} ({entry['bundleSize']} bytes)")
    return 0


def cmd_install_candidate(args: argparse.Namespace) -> int:
    try:
        bundle_path, _bundle, bundle_info, previous, candidate = validate_install_candidate(
            args.store,
            args.candidate,
            args.max_bundle_bytes,
            allow_untrusted=args.allow_untrusted_signature,
            allow_downgrade=args.allow_downgrade,
        )
    except SystemExit as error:
        if args.report:
            try:
                candidate = load_install_candidate(args.candidate)
                bundle_path = candidate["bundlePath"]
                bundle = read_bundle(bundle_path, args.max_bundle_bytes)
                bundle_info = parse_jfapp(bundle)
                previous = existing_app_entry(args.store, bundle_info["summary"]["id"])
                reason = str(error).removeprefix("jellyframe_app_registry: ").split(":")[0].strip()
                atomic_write_json(
                    args.report,
                    build_failed_install_transaction_report(
                        args.store,
                        bundle_path,
                        bundle_info,
                        previous,
                        reason or "candidate-rejected",
                        source_kind="install-candidate",
                        preflight_report=str(args.candidate),
                        allow_downgrade=args.allow_downgrade,
                    ),
                )
            except SystemExit:
                pass
        raise
    entry = install_bundle(
        args.store,
        bundle_path,
        args.max_apps,
        args.max_bundle_bytes,
        allow_downgrade=args.allow_downgrade,
    )
    if args.report:
        report = build_install_transaction_report(
            args.store,
            bundle_path,
            entry,
            previous,
            source_kind="install-candidate",
            preflight_report=str(args.candidate),
            allow_downgrade=args.allow_downgrade,
        )
        report["candidate"] = {
            "path": str(args.candidate),
            "signatureStatus": candidate_signature_status(candidate),
            "userApproval": bool(candidate.get("userApproval")),
            "download": candidate.get("download", {}) if isinstance(candidate.get("download", {}), dict) else {},
        }
        atomic_write_json(args.report, report)
    if args.json:
        print(json.dumps(entry, ensure_ascii=False, indent=2))
    else:
        print(f"installed-candidate {entry['id']} {entry['versionName']} ({entry['bundleSize']} bytes)")
    return 0


def cmd_list(args: argparse.Namespace) -> int:
    registry = sorted_registry(load_registry(args.store.resolve()))
    if args.json:
        print(json.dumps(registry, ensure_ascii=False, indent=2))
    else:
        apps = registry.get("apps", [])
        if not apps:
            print("no installed apps")
        for app in apps:
            status = app.get("status", APP_STATUS_INSTALLED)
            rollback = " rollback-ready" if isinstance(app.get("rollback"), dict) else ""
            print(
                f"{app.get('id')} {app.get('versionName')} {status}{rollback} "
                f"{app.get('name')} {app.get('bundleSize')} bytes"
            )
    return 0


def cmd_state(args: argparse.Namespace) -> int:
    state = app_manager_state_from_registry(args.store, load_registry(args.store.resolve()))
    if args.output:
        atomic_write_json(args.output, state)
    if args.json or not args.output:
        print(json.dumps(state, ensure_ascii=False, indent=2))
    else:
        summary = state["summary"]
        print(
            f"state apps={summary['appCount']} launchable={summary['launchableCount']} "
            f"failed={summary['failedCount']} rollback-ready={summary['rollbackReadyCount']}"
        )
    return 0


def cmd_remove(args: argparse.Namespace) -> int:
    entry = remove_app(args.store, args.app_id, delete_data=not args.keep_data)
    if args.json:
        print(json.dumps(entry, ensure_ascii=False, indent=2))
    else:
        suffix = " data-retained" if entry.get("dataRetained") else " data-deleted"
        print(f"removed {entry.get('id')}{suffix}")
    return 0


def cmd_delete_data(args: argparse.Namespace) -> int:
    deleted = delete_app_data(args.store, args.app_id)
    result = {"id": args.app_id, "dataDeleted": deleted}
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print(f"deleted-data {args.app_id}" if deleted else f"no-data {args.app_id}")
    return 0


def cmd_rollback(args: argparse.Namespace) -> int:
    entry = rollback_app(args.store, args.app_id)
    if args.json:
        print(json.dumps(entry, ensure_ascii=False, indent=2))
    else:
        print(f"rolled-back {entry.get('id')} {entry.get('versionName')}")
    return 0


def cmd_enable(args: argparse.Namespace) -> int:
    entry = set_app_enabled(args.store, args.app_id, True)
    if args.json:
        print(json.dumps(entry, ensure_ascii=False, indent=2))
    else:
        print(f"enabled {entry.get('id')}")
    return 0


def cmd_disable(args: argparse.Namespace) -> int:
    entry = set_app_enabled(args.store, args.app_id, False)
    if args.json:
        print(json.dumps(entry, ensure_ascii=False, indent=2))
    else:
        print(f"disabled {entry.get('id')}")
    return 0


def cmd_mark_failed(args: argparse.Namespace) -> int:
    entry = mark_app_failed(args.store, args.app_id, args.reason, args.message)
    if args.json:
        print(json.dumps(entry, ensure_ascii=False, indent=2))
    else:
        print(f"failed {entry.get('id')} {entry.get('failure', {}).get('reason', '')}")
    return 0


def cmd_path(args: argparse.Namespace) -> int:
    print(app_bundle_path(args.store, args.app_id))
    return 0


def add_store_arg(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--store", required=True, type=Path, help="Installed-app registry directory.")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="JellyFrame desktop installed-app registry mock.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    install = subparsers.add_parser("install", help="Install or update a .jfapp bundle.")
    add_store_arg(install)
    install.add_argument("--bundle", required=True, type=Path, help="Input .jfapp bundle.")
    install.add_argument("--max-apps", type=int, default=DEFAULT_MAX_APPS, help="Maximum installed apps.")
    install.add_argument("--max-bundle-bytes", type=int, default=DEFAULT_MAX_BUNDLE_BYTES,
                         help="Maximum accepted bundle size.")
    install.add_argument("--json", action="store_true", help="Print installed entry as JSON.")
    install.add_argument("--report", type=Path, help="Write an install transaction JSON report.")
    install.add_argument("--allow-downgrade", action="store_true",
                         help="Allow installing a lower versionCode over the current app.")
    install.set_defaults(func=cmd_install)

    install_candidate = subparsers.add_parser(
        "install-candidate",
        help="Validate a host-prepared install candidate JSON and install its local .jfapp bundle.",
    )
    add_store_arg(install_candidate)
    install_candidate.add_argument("--candidate", required=True, type=Path,
                                   help="Host-prepared install candidate JSON.")
    install_candidate.add_argument("--max-apps", type=int, default=DEFAULT_MAX_APPS, help="Maximum installed apps.")
    install_candidate.add_argument("--max-bundle-bytes", type=int, default=DEFAULT_MAX_BUNDLE_BYTES,
                                   help="Maximum accepted bundle size.")
    install_candidate.add_argument("--allow-downgrade", action="store_true",
                                   help="Allow installing a lower versionCode over the current app.")
    install_candidate.add_argument("--allow-untrusted-signature", action="store_true",
                                   help="Permit unsigned/untrusted candidates for desktop bring-up only.")
    install_candidate.add_argument("--json", action="store_true", help="Print installed entry as JSON.")
    install_candidate.add_argument("--report", type=Path, help="Write an install transaction JSON report.")
    install_candidate.set_defaults(func=cmd_install_candidate)

    list_apps = subparsers.add_parser("list", help="List installed apps.")
    add_store_arg(list_apps)
    list_apps.add_argument("--json", action="store_true", help="Print registry JSON.")
    list_apps.set_defaults(func=cmd_list)

    state = subparsers.add_parser("state", help="Print a launcher-friendly app manager state report.")
    add_store_arg(state)
    state.add_argument("--json", action="store_true", help="Print state JSON.")
    state.add_argument("--output", type=Path, help="Write state JSON to a file.")
    state.set_defaults(func=cmd_state)

    remove = subparsers.add_parser("remove", help="Remove an installed app.")
    add_store_arg(remove)
    remove.add_argument("--id", dest="app_id", required=True, help="Installed app id.")
    remove.add_argument("--keep-data", action="store_true", help="Keep app-private data after removing the bundle.")
    remove.add_argument("--json", action="store_true", help="Print removed entry as JSON.")
    remove.set_defaults(func=cmd_remove)

    delete_data = subparsers.add_parser("delete-data", help="Delete app-private data without removing the bundle.")
    add_store_arg(delete_data)
    delete_data.add_argument("--id", dest="app_id", required=True, help="Installed app id.")
    delete_data.add_argument("--json", action="store_true", help="Print deletion result as JSON.")
    delete_data.set_defaults(func=cmd_delete_data)

    rollback = subparsers.add_parser("rollback", help="Rollback an installed app to its previous bundle.")
    add_store_arg(rollback)
    rollback.add_argument("--id", dest="app_id", required=True, help="Installed app id.")
    rollback.add_argument("--json", action="store_true", help="Print restored entry as JSON.")
    rollback.set_defaults(func=cmd_rollback)

    enable = subparsers.add_parser("enable", help="Enable an installed app for launch.")
    add_store_arg(enable)
    enable.add_argument("--id", dest="app_id", required=True, help="Installed app id.")
    enable.add_argument("--json", action="store_true", help="Print updated entry as JSON.")
    enable.set_defaults(func=cmd_enable)

    disable = subparsers.add_parser("disable", help="Disable an installed app without deleting data or bundles.")
    add_store_arg(disable)
    disable.add_argument("--id", dest="app_id", required=True, help="Installed app id.")
    disable.add_argument("--json", action="store_true", help="Print updated entry as JSON.")
    disable.set_defaults(func=cmd_disable)

    mark_failed = subparsers.add_parser("mark-failed", help="Mark an installed app as failed and not launchable.")
    add_store_arg(mark_failed)
    mark_failed.add_argument("--id", dest="app_id", required=True, help="Installed app id.")
    mark_failed.add_argument("--reason", required=True, help="Stable failure reason.")
    mark_failed.add_argument("--message", default="", help="Human-readable failure detail.")
    mark_failed.add_argument("--json", action="store_true", help="Print updated entry as JSON.")
    mark_failed.set_defaults(func=cmd_mark_failed)

    path = subparsers.add_parser("path", help="Print the installed bundle path for an app.")
    add_store_arg(path)
    path.add_argument("--id", dest="app_id", required=True, help="Installed app id.")
    path.set_defaults(func=cmd_path)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
