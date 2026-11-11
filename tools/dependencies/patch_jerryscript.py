#!/usr/bin/env python3
"""Apply JellyFrame's reviewed WeakRef initialization fix to pinned JerryScript."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--check', action='store_true', help='Validate without applying')
    args = parser.parse_args()
    root = args.source_root.resolve(strict=True)
    directory = Path(__file__).resolve().parent
    manifest = json.loads((directory / 'jerryscript-weakref-initialization.json').read_text(encoding='utf-8'))
    before = True
    after = True
    for name, expected in manifest['files'].items():
        content = (root / name).read_text(encoding='utf-8')
        digest = hashlib.sha256(content.encode('utf-8')).hexdigest()
        before = before and digest == expected['before']
        after = after and digest == expected['after']
    if after:
        print('JerryScript WeakRef initialization patch is already applied')
        return
    if not before:
        raise SystemExit('JerryScript files differ from the pinned patch inputs or are partially patched')
    patch = directory / 'jerryscript-weakref-initialization.patch'
    environment = os.environ.copy()
    # A build-directory overlay may be inside the Runtime repository. Apply
    # against the overlay itself instead of discovering that enclosing repo.
    environment['GIT_CEILING_DIRECTORIES'] = str(root.parent)
    subprocess.run(['git', 'apply', '--check', str(patch)], cwd=root, env=environment, check=True)
    if args.check:
        print('Pinned JerryScript files match and the WeakRef patch can be applied')
        return
    subprocess.run(['git', 'apply', str(patch)], cwd=root, env=environment, check=True)
    for name, expected in manifest['files'].items():
        digest = hashlib.sha256((root / name).read_text(encoding='utf-8').encode('utf-8')).hexdigest()
        if digest != expected['after']:
            raise SystemExit(f'Patched file verification failed: {name}')
    print('Applied and verified JerryScript WeakRef initialization patch')


if __name__ == '__main__':
    main()
