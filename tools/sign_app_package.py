#!/usr/bin/env python3
"""Create a JFSG v1 envelope over an existing, immutable JFAPP bundle.

The private key is local PEM input, never an output or a provisioning operation.
Signing does not add the public key to any target's trust table.
"""
import argparse
import getpass
import hashlib
import json
from pathlib import Path
import re
import struct
import unicodedata
import zlib

import app_registry

DOMAIN = b"JellyFrame Signed Package v1\0"
MAX_PACKAGE_BYTES = 4 * 1024 * 1024


def reject(message):
    raise ValueError(message)


def text_bytes(value, maximum, label):
    if not isinstance(value, str) or not value:
        reject(f"{label} must be nonempty text")
    try:
        encoded = value.encode("utf-8")
    except UnicodeError:
        reject(f"{label} must be valid UTF-8")
    if len(encoded) > maximum or any(unicodedata.category(c) == "Cc" for c in value):
        reject(f"{label} exceeds its byte limit or contains controls")
    return encoded


def bundle_facts(bundle):
    if not isinstance(bundle, bytes) or len(bundle) > MAX_PACKAGE_BYTES:
        reject("bundle must be immutable bytes within 4 MiB")
    # Existing registry parser owns normalized summary and container-format checks.
    try:
        parsed = app_registry.parse_jfapp(bundle)
    except (SystemExit, UnicodeError, struct.error) as error:
        raise ValueError("invalid JFAPP bundle") from error
    header = struct.unpack_from(app_registry.JFAPP_HEADER_FORMAT, bundle)
    (_, _, _, _, summary_offset, summary_size, index_offset, count,
     strings_offset, strings_size, payload_offset, payload_size, crc, _) = header
    if count == 0 or count > 4096 or summary_size > 4096 or crc == 0:
        reject("bundle exceeds device summary/resource limits or has no checksum")
    for offset in (summary_offset, index_offset, strings_offset, payload_offset):
        if offset < app_registry.JFAPP_HEADER_SIZE:
            reject("bundle section overlaps its header")
    resources = []
    indexed_paths = []
    seen = set()
    for index in range(count):
        (path_hash, path_offset, path_size, _kind, offset, size, resource_crc, flags) = struct.unpack_from(
            app_registry.JFAPP_ENTRY_FORMAT, bundle, index_offset + index * app_registry.JFAPP_ENTRY_SIZE)
        if flags or path_size == 0 or path_size > 255:
            reject("invalid resource flags/path size")
        raw_path = bundle[strings_offset + path_offset:strings_offset + path_offset + path_size]
        try:
            path = raw_path.decode("utf-8")
        except UnicodeError as error:
            raise ValueError("resource path is not UTF-8") from error
        text_bytes(path, 255, "resource path")
        if (not path.startswith("/") or "\\" in path or ":" in path or
                any(part in ("", ".", "..") for part in path[1:].split("/"))):
            reject("resource path must be normalized absolute UTF-8")
        if raw_path in seen:
            reject("duplicate resource path")
        seen.add(raw_path)
        indexed_paths.append(raw_path)
        expected_hash = 0x811c9dc5
        for byte in raw_path:
            expected_hash = ((expected_hash ^ byte) * 0x01000193) & 0xffffffff
        payload = bundle[payload_offset + offset:payload_offset + offset + size]
        if path_hash != expected_hash or zlib.crc32(payload) & 0xffffffff != resource_crc:
            reject("resource hash or checksum mismatch")
        resources.append((raw_path, hashlib.sha256(payload).digest()))
    resources.sort(key=lambda item: item[0])
    # The real device currently requires the resource index in this same order.
    if indexed_paths != [path for path, _ in resources]:
        reject("device signed resource index must be sorted by unsigned UTF-8 bytes")
    summary = parsed["summary"]
    entry = text_bytes(summary["entry"], 191, "entry")
    if entry not in seen:
        reject("bundle entry is absent from its resources")
    return parsed, resources


def sign_bundle(bundle, private_key, key_id):
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    if not isinstance(private_key, ec.EllipticCurvePrivateKey) or not isinstance(private_key.curve, ec.SECP256R1):
        reject("JFSG requires an ECDSA P-256 private key")
    if not isinstance(key_id, bytes) or len(key_id) != 16 or not any(key_id):
        reject("key id must be 16 nonzero-identifying bytes")
    parsed, resources = bundle_facts(bundle)
    summary = parsed["summary"]
    def sized(value, maximum, label):
        raw = text_bytes(value, maximum, label)
        return struct.pack("<H", len(raw)) + raw
    version_code = summary["versionCode"]
    if not 0 <= version_code <= 0xffffffff:
        reject("versionCode exceeds u32")
    manifest = (sized(summary["id"], 95, "app id") + sized(summary["versionName"], 63, "version") +
                struct.pack("<I", version_code) + hashlib.sha256(bundle).digest() +
                struct.pack("<I", len(resources)))
    manifest += b"".join(struct.pack("<H", len(path)) + path + digest for path, digest in resources)
    if len(manifest) > 1024 * 1024 or 128 + len(manifest) + len(bundle) > MAX_PACKAGE_BYTES:
        reject("signed package exceeds JFSG limits")
    header = (b"JFSG\0\0\0\0" + struct.pack("<HHHHIHH", 1, 64, 1, 0, len(manifest), 64, 0) +
              key_id + bytes(24))
    message = DOMAIN + header + manifest
    der = private_key.sign(message, ec.ECDSA(hashes.SHA256()))
    private_key.public_key().verify(der, message, ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(der)
    signature = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    return header + manifest + signature + bundle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--private-key", type=Path, required=True, help="Local P-256 PEM; never copied into output")
    parser.add_argument("--key-id", required=True, help="32 hexadecimal characters matching a reviewed target key id")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--encrypted-key", action="store_true", help="Prompt locally for the PEM password")
    args = parser.parse_args()
    try:
        from cryptography.hazmat.primitives import serialization
        if args.output.resolve() in (args.bundle.resolve(), args.private_key.resolve()):
            reject("output must not overwrite the bundle or private key")
        if args.bundle.stat().st_size > MAX_PACKAGE_BYTES:
            reject("bundle exceeds 4 MiB")
        if args.private_key.stat().st_size > 16384:
            reject("PEM key exceeds 16 KiB")
        if not re.fullmatch(r"[0-9a-fA-F]{32}", args.key_id):
            reject("key id must contain exactly 32 hexadecimal characters")
        key_id = bytes.fromhex(args.key_id)
        password = getpass.getpass("PEM password: ").encode() if args.encrypted_key else None
        key = serialization.load_pem_private_key(args.private_key.read_bytes(), password)
        package = sign_bundle(args.bundle.read_bytes(), key, key_id)
        # Refuse replacement of prior artifacts and leave inputs untouched.
        with args.output.open("xb") as output:
            output.write(package)
        print(json.dumps({"format": "JFSG", "version": 1, "bytes": len(package),
                          "sha256": hashlib.sha256(package).hexdigest(), "keyId": key_id.hex()}))
    except (ValueError, TypeError, OSError) as error:
        parser.exit(1, f"sign_app_package: {error}\n")
    except ImportError:
        parser.exit(1, "sign_app_package: install the optional cryptography package for offline signing\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
