"""Bounded install identity inspection, without signature trust decisions.

The device remains the authentication authority. Hash/CRC agreement here detects
malformed transport inputs; it does not prove who signed an envelope.
This module has only standard-library dependencies and ships with the provider.
"""
import hashlib
import json
from pathlib import Path
import struct
import unicodedata
import zlib

MAX_PACKAGE_BYTES = 4 * 1024 * 1024
HEADER = struct.Struct('<8sHHIIIIIIIIIII')
ENTRY = struct.Struct('<IIHHIIII')


def _reject(message):
    raise ValueError(message)


def _text(raw, maximum, label):
    if not raw or len(raw) > maximum:
        _reject('invalid ' + label + ' size')
    value = raw.decode('utf-8')
    if any(unicodedata.category(c) == 'Cc' for c in value):
        _reject('controls in ' + label)
    return value


def _path(raw):
    value = _text(raw, 255, 'path')
    if (not value.startswith('/') or '\\' in value or ':' in value or
            any(part in ('', '.', '..') for part in value[1:].split('/'))):
        _reject('invalid resource path')
    return value


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            _reject('duplicate summary member')
        result[key] = value
    return result


def _bundle(raw):
    if len(raw) < HEADER.size:
        _reject('truncated JFAPP')
    (magic, size, version, flags, summary_at, summary_size, index_at, count,
     strings_at, strings_size, payload_at, payload_size, crc, reserved) = HEADER.unpack_from(raw)
    if magic != b'JFAPPV0\0' or size != HEADER.size or version not in (0, 1) or flags or reserved:
        _reject('invalid JFAPP header')
    if not 0 < summary_size <= 4096 or not 0 < count <= 128:
        _reject('JFAPP device summary/resource limit')
    for offset, length in ((summary_at, summary_size), (index_at, count * ENTRY.size),
                           (strings_at, strings_size), (payload_at, payload_size)):
        if offset < HEADER.size or offset > len(raw) or length > len(raw) - offset:
            _reject('JFAPP section out of bounds')
    check = bytearray(raw)
    check[48:52] = bytes(4)
    if not crc or zlib.crc32(check) & 0xffffffff != crc:
        _reject('JFAPP checksum mismatch')
    summary = json.loads(raw[summary_at:summary_at + summary_size].decode('utf-8'),
                         object_pairs_hook=_unique)
    if not isinstance(summary, dict):
        _reject('invalid JFAPP summary')
    app = summary.get('id')
    if not isinstance(app, str) or not app.isascii():
        _reject('invalid app id')
    _text(app.encode(), 95, 'app id')
    name, code = summary.get('versionName'), summary.get('versionCode')
    if not isinstance(name, str) or type(code) is not int or not 0 <= code <= 0xffffffff:
        _reject('invalid version metadata')
    _text(name.encode('utf-8'), 63, 'version')
    pipeline = summary.get('textPipeline')
    if version != int('textPipeline' in summary):
        _reject('JFAPP format/pipeline mismatch')
    if version and (not isinstance(pipeline, dict) or pipeline.get('kind') not in ('jfsp1-block-v11', 'jfsp2-block-v12') or
                    summary.get('script') != 'classic'):
        _reject('unsupported shaped pipeline')
    resources = []
    resource_sizes = {}
    for index in range(count):
        path_hash, at, length, kind, offset, length_bytes, resource_crc, entry_flags = ENTRY.unpack_from(
            raw, index_at + index * ENTRY.size)
        if entry_flags or at > strings_size or length > strings_size - at:
            _reject('invalid resource index')
        if offset > payload_size or length_bytes > payload_size - offset:
            _reject('resource payload out of bounds')
        path = raw[strings_at + at:strings_at + at + length]
        _path(path)
        if resources and path <= resources[-1][0]:
            _reject('resource index not strictly sorted')
        fnv = 0x811c9dc5
        for byte in path:
            fnv = ((fnv ^ byte) * 0x01000193) & 0xffffffff
        content = raw[payload_at + offset:payload_at + offset + length_bytes]
        if fnv != path_hash or zlib.crc32(content) & 0xffffffff != resource_crc:
            _reject('resource hash/checksum mismatch')
        resources.append((path, hashlib.sha256(content).digest()))
        resource_sizes[path] = len(content)
    entry = summary.get('entry')
    if not isinstance(entry, str) or entry.encode('utf-8') not in {path for path, _ in resources}:
        _reject('entry missing from resources')
    if version:
        fields = {'kind', 'font', 'catalog', 'companion', 'tables', 'eligibility',
                  'sourceIdentity', 'profileIdentity', 'glyphCount', 'resourceId',
                  'rasterSize', 'weight', 'selection'}
        if fields - set(pipeline) or set(pipeline) - fields - {'fontFamilyHash'}:
            _reject('invalid shaped declaration fields')
        paths = []
        for field in ('font', 'catalog', 'companion', 'tables', 'eligibility'):
            value = pipeline[field]
            if not isinstance(value, str):
                _reject('invalid shaped resource path')
            path = value.encode('utf-8')
            _path(path)
            if not resource_sizes.get(path) or path in paths:
                _reject('missing or duplicate shaped resource')
            paths.append(path)
        for field in ('sourceIdentity', 'profileIdentity'):
            value = pipeline[field]
            if (not isinstance(value, str) or len(value) != 64 or
                    any(c not in '0123456789abcdef' for c in value) or not int(value, 16)):
                _reject('invalid shaped identity')
        for field, (low, high) in {
                'glyphCount': (1, 65536), 'resourceId': (1, 0xffffffff),
                'rasterSize': (1, 65535), 'weight': (1, 1000), 'selection': (0, 65535),
                'fontFamilyHash': (0, 0xffffffff)}.items():
            value = pipeline.get(field, 0)
            if type(value) is not int or not low <= value <= high:
                _reject('invalid shaped numeric metadata')
    return summary, resources, version


def inspect_install_bytes(raw, max_bytes, *, allow_shaped=False):
    if type(max_bytes) is not int or not 0 < max_bytes <= MAX_PACKAGE_BYTES:
        _reject('invalid target capacity')
    if not isinstance(raw, bytes) or not 0 < len(raw) <= max_bytes:
        _reject('package exceeds target capacity')
    signed = raw[:8] == b'JFSG\0\0\0\0'
    bundle = raw
    if signed:
        if len(raw) < 128 or struct.unpack_from('<HHHH', raw, 8) != (1, 64, 1, 0):
            _reject('invalid JFSG header')
        manifest_size, signature_size, reserved = struct.unpack_from('<IHH', raw, 16)
        if (not 0 < manifest_size <= 1024 * 1024 or signature_size != 64 or reserved or
                any(raw[40:64]) or not any(raw[24:40]) or 128 + manifest_size >= len(raw)):
            _reject('invalid JFSG envelope bounds')
        manifest = raw[64:64 + manifest_size]
        bundle = raw[128 + manifest_size:]
    summary, resources, version = _bundle(bundle)
    if version and (not signed or not allow_shaped):
        _reject('shaped install requires signed envelope and matching text.shaping capability')
    if signed:
        cursor = 0
        def read(size):
            nonlocal cursor
            if size > len(manifest) - cursor:
                _reject('truncated signed manifest')
            value = manifest[cursor:cursor + size]
            cursor += size
            return value
        def sized(maximum, label):
            return _text(read(struct.unpack('<H', read(2))[0]), maximum, label)
        app, name = sized(95, 'app id'), sized(63, 'version')
        code = struct.unpack('<I', read(4))[0]
        if ((app, name, code) != (summary.get('id'), summary.get('versionName'), summary.get('versionCode')) or
                read(32) != hashlib.sha256(bundle).digest() or
                struct.unpack('<I', read(4))[0] != len(resources)):
            _reject('signed metadata does not match inner bundle')
        for path, digest in resources:
            size = struct.unpack('<H', read(2))[0]
            if read(size) != path or read(32) != digest:
                _reject('signed resource metadata mismatch')
        if cursor != len(manifest):
            _reject('trailing signed manifest bytes')
    return {'appId': summary['id'], 'formatVersion': version, 'signedEnvelope': signed,
            'signatureVerified': False, 'bytes': len(raw)}


def read_install_package(path, max_bytes, *, allow_shaped=False):
    path = Path(path)
    if not path.is_absolute() or path.suffix.lower() not in ('.jfapp', '.jfsg'):
        _reject('install requires an absolute .jfapp or .jfsg path')
    # Read once with a hard bound; the same immutable bytes are inspected and
    # sent. A later replacement of the file cannot change the transmitted app.
    if type(max_bytes) is not int or not 0 < max_bytes <= MAX_PACKAGE_BYTES:
        _reject('invalid target capacity')
    with path.open('rb') as source:
        raw = source.read(max_bytes + 1)
    facts = inspect_install_bytes(raw, max_bytes, allow_shaped=allow_shaped)
    if facts['signedEnvelope'] != (path.suffix.lower() == '.jfsg'):
        _reject('package extension does not match container')
    return raw, facts
