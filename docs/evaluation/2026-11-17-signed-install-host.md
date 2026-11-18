# Signed package creation and actual-store host qualification

Local/unpublished checkpoint,2026-11-17. Production keys, dependency locks,
defaults and device images are untouched. No serial enumeration or flashing.

`tools/sign_app_package.py` creates JFSG v1 over the exact original JFAPP bytes,
using a local ECDSA P-256 PEM and explicit16-byte key id. It admits both scalar
format0 and shaped format1, validates summary/header pairing through the shared
registry parser, then checks resource paths/hash/CRC, uniqueness, unsigned-UTF8
index order, entry presence, bounded counts/sections and envelope limits. The
signed manifest records app/version, whole-bundle SHA-256 and every resource's
SHA-256. Domain,64-byte header and manifest are signed with SHA-256, including
the domain's terminating NUL; the signature is raw32-byte r then32-byte s.

Example, with an independently provisioned target key id:

```powershell
python tools/sign_app_package.py --bundle app.jfapp --private-key local-key.pem `
  --key-id 0123456789abcdef0123456789abcdef --output app.jfsg
```

`--encrypted-key` prompts locally for an encrypted PEM password. The CLI never
accepts a password argument, writes/copies a key, provisions a target or replaces
existing output. It refuses output paths equal to either input. Signing requires
the optional Python cryptography dependency. Output reports only package hash,
size, format and public key id. This example is syntax, not a usable trust key.
The default production allowlist is still empty; merely signing does not grant
target trust.

The new standalone suite in `ports/esp32s3-idf/tests/signed_install` feeds these
complete containers through the actual WS147 store and install transaction.
The real mbedTLS adapter validates the signature with public test-key injection
in a separately compiled host-only object; the unmodified production callback
rejects the same package. Tests cover commits, reconstruction, leases with exact
authenticated digests/key, transaction ordering, rollback, signature mutation,
wrong key, interrupted commit and snapshot allocation refusal. Boot tampering
is checked both with stale CRC and with envelope/registry CRCs recomputed so the
signature itself is the rejecting boundary.

Results:18/18 CTest cases, including10 install cases (838 explicit checks,65
callback calls), existing6 actual-crypto cases (289 checks),5 Python signing
tests and a production-object symbol check. Details, exact commands and
simulation limits are in the suite README. This is host memory-backed NOR
simulation. It does not close physical device acceptance or qualify execution
of the placeholder shaping assets.

Build: `E:/Builds/runtime-jfsg-installed-host-20261117`.
Final log: `E:/Temp/runtime-jfsg-installed-host-ctest-final3-20261117.log`.
Persistent evidence: `E:/Experiments/migration-signed-install-host-20261117`.
The production parser/store/crypto sources were not modified by this slice.

Remaining migration gates: signed physical transport and320KiB store/provider
capacity adaptation for real catalog packages, positive device install/reboot/
tamper/fallback, actual shaped worker memory/OOM/recovery/latency, newly versioned published
packages and hosted CI (local full/cropped consumption now passes34/34 each), ABI9 canonical identity/contextual
shaping/Layout/paint integration. Ordinary ABI4 generation200 acceptance stays
closed. Provisioning and replay-resistant anti-rollback remain separate policy
work; the current version check is relative to recovered active state.

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

Capacity follow-up: [explicit storage capacity](2026-11-17-signed-install-capacity.md) now passes22/22 host checks with a separate opt-in target compile; default provider transport and real shaping assets remain unqualified.
