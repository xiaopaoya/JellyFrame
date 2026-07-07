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
JFAPP_ENTRY_SIZE = 28
REGISTRY_FORMAT = "jellyframe.installed_apps.registry"
REGISTRY_VERSION = 0
APP_STATUS_INSTALLED = "installed"
APP_STATUS_DISABLED = "disabled"
APP_STATUS_FAILED = "failed"
DEFAULT_MAX_APPS = 32
DEFAULT_MAX_BUNDLE_BYTES = 4 * 1024 * 1024


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


def read_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except json.JSONDecodeError as error:
        fail(f"invalid JSON {path}: {error}")


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
    registry["apps"] = apps
    return registry


def sorted_registry(registry: dict) -> dict:
    registry = dict(registry)
    registry["apps"] = sorted(registry.get("apps", []), key=lambda app: str(app.get("id", "")))
    return registry


def read_bundle(path: Path, max_bundle_bytes: int) -> bytes:
    if not path.is_file():
        fail(f"bundle does not exist: {path}")
    size = path.stat().st_size
    if max_bundle_bytes > 0 and size > max_bundle_bytes:
        fail(f"bundle exceeds max bytes: {path} ({size} > {max_bundle_bytes})")
    return path.read_bytes()


def byte_range_is_valid(total: int, offset: int, size: int) -> bool:
    return 0 <= offset <= total and 0 <= size <= total - offset


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


def install_bundle(store: Path, bundle_path: Path, max_apps: int, max_bundle_bytes: int) -> dict:
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

    final_name = bundle_filename(bundle_info["summary"], bundle_info["sha256"])
    final_path = bundles_dir(store) / final_name
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
        obsolete_path = bundles_dir(store) / obsolete_rollback_file
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
) -> dict:
    rollback = entry.get("rollback")
    rollback_available = isinstance(rollback, dict) and bool(rollback.get("bundleFile"))
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
    if isinstance(bundle_file, str) and bundle_file:
        path = bundles_dir(store) / bundle_file
        if path.exists():
            path.unlink()
    rollback = entry.get("rollback", {})
    if isinstance(rollback, dict):
        rollback_file = rollback.get("bundleFile")
        if isinstance(rollback_file, str) and rollback_file and rollback_file != bundle_file:
            rollback_path = bundles_dir(store) / rollback_file
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
    rollback_path = bundles_dir(store) / str(rollback_file)
    if not rollback_path.is_file():
        fail(f"rollback bundle is missing: {rollback_path}")
    restored = apply_rollback_record(entry, rollback)
    apps[apps.index(entry)] = restored
    atomic_write_json(registry_path(store), sorted_registry(registry))
    return restored


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
    path = bundles_dir(store.resolve()) / bundle_file
    if not path.is_file():
        fail(f"installed app bundle is missing: {path}")
    return path


def cmd_install(args: argparse.Namespace) -> int:
    bundle = read_bundle(args.bundle, args.max_bundle_bytes)
    bundle_info = parse_jfapp(bundle)
    previous = existing_app_entry(args.store, bundle_info["summary"]["id"])
    entry = install_bundle(args.store, args.bundle, args.max_apps, args.max_bundle_bytes)
    if args.report:
        atomic_write_json(
            args.report,
            build_install_transaction_report(args.store, args.bundle, entry, previous),
        )
    if args.json:
        print(json.dumps(entry, ensure_ascii=False, indent=2))
    else:
        print(f"installed {entry['id']} {entry['versionName']} ({entry['bundleSize']} bytes)")
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
    install.set_defaults(func=cmd_install)

    list_apps = subparsers.add_parser("list", help="List installed apps.")
    add_store_arg(list_apps)
    list_apps.add_argument("--json", action="store_true", help="Print registry JSON.")
    list_apps.set_defaults(func=cmd_list)

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
