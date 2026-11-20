# Actual JFSG ECDSA adapter qualification

This standalone host suite compiles the device adapter and the JFSG parser
against the software primitives from the local ESP-IDF mbedTLS source tree.
It does not substitute a fake signature callback. It runs on Windows; ESP
hardware acceleration, flash recovery and device allocation behavior are not
qualified here.

The default firmware allowlist is empty. Test-only key injection calls the
same private hash/ECDSA implementation and is compiled only in this executable.
A separate object target compiles the production translation unit without the
test macro; its symbol table must not expose the injection function. The
production callback rejects even the valid test vector, regardless of context.

The configured-trust executable uses the public manifest and matching signature
from a separate disposable fixture. It checks production callback acceptance
and refusal for a wrong id, a changed signature, and a mismatched public point.
The Python test exercises manifest validation and deterministic regeneration,
including empty trust after clearing a previously configured manifest.

`generate_vectors.py` uses cryptography/OpenSSL deterministic ECDSA with public
scalars 1 and 2. These are deliberately public test keys, never provisioning
material. The checked-in header requires no Python cryptography dependency.
Its parser payload is a bounded synthetic parser fixture, not an installable
JFAPP application. Device bundle hashing and boot recovery require separate
end-to-end tests.

Configure and run (after the development environment is loaded):

```powershell
cmake -S ports/esp32s3-idf/tests/package_signature `
  -B E:/Builds/runtime-jfsg-real-crypto-20261117 -G Ninja `
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
  -DCMAKE_BUILD_TYPE=Release `
  -DJFSG_MBEDTLS_SOURCE_DIR=D:/CodexTemp/abi4-mainline-20261115/idf-v5.3.1/components/mbedtls/mbedtls
cmake --build E:/Builds/runtime-jfsg-real-crypto-20261117
ctest --test-dir E:/Builds/runtime-jfsg-real-crypto-20261117 -V
```

Qualification executed 2026-11-17: 6/6 cases, 289 checks, assertions independent
of NDEBUG. Cases cover a correct vector/hash, every domain/header/manifest byte
mutated, missing domain NUL, signature made without the NUL, truncation,
wrong key/id, off-curve public key, each signature byte mutated, zero/oversized
r and s, DER-as-raw refusal, null/empty arguments, real parser success and
transactional rejection. The valid parser result is refused after signature
mutation and by the empty production allowlist.

Library version: mbedTLS 3.6.0 from IDF5.3.1. `library/ecdsa.c` SHA-256:
`70d850267ed88104c5e35aa3532e7e033913710bc82d5248f30acdb498720e5a`.
Canonical vector message SHA-256:
`e780cb380d8ebf85e52ee4e45aaf15134c92d8c4617cd5916570d346e012dc6c`.
Verbose test log: `E:/Builds/runtime-jfsg-real-crypto-20261117/qualification-ctest.log`.

A later actual-store host integration uses complete scalar/shaped containers,
transaction/staging/commit, reconstruction, authenticated leases and tamper
fallback; see `../signed_install/README.md`. It does not replace this suite's
original evidence or establish device execution.

Still open: exact-device crypto execution, positive signed install/reboot/
tamper/fallback probes, provisioning policy and replay-resistant anti-rollback.

Last updated: 2026-11-17; Applies to: 0.6.0-dev.
