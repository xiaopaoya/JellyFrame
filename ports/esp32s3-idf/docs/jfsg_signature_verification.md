# JFSG device verification

The ESP32-S3 install store accepts only JFSG v1 envelopes. It verifies the
signature over the NUL-terminated domain, fixed header, and exact manifest;
then it validates the embedded JFAPPV0 descriptor and compares SHA-256 for the
complete bundle and every resource against the signed manifest. Registry
publication occurs only after all checks pass. JFDP's transport CRC is checked
for transfer integrity and is never used as identity or authorization.

The ECDSA adapter consumes the protocol's 64-byte raw `r || s` signature. It
loads each scalar as a 32-byte big-endian MPI and calls the mbedTLS ECDSA
verification API; DER encoding is not accepted. Keys are selected by the
16-byte key id.

The production public-key allowlist is currently empty because no product
public key has been provisioned. Therefore every signed-package install fails
closed until reviewed product public keys and key ids are added to
`jellyframe_esp32s3_package_signature.cpp`. This build does not provide signed
package authenticity. Do not place production private keys in device firmware
or this repository. Any test vector key must remain test-only and must not be
added to the production allowlist.

Signed-package downgrade authorization is not represented by the unsigned
JFDP `allow_downgrade` flag. The device rejects a package whose signed
`version_code` is below the active package for the same app. A future rollback
mechanism must be an explicit signed contract. This comparison does not provide
replay-resistant anti-rollback: restoring an older valid registry record and its
signed envelope can make that older version the active record again. Registry
CRC detects accidental corruption, and signatures authenticate package bytes;
neither is monotonic trusted state. If flash replay is in the threat model,
provision a monotonic secure version/generation source before claiming
anti-rollback protection.

The registry stores the exact envelope length, payload offset, and envelope
CRC. The CRC only detects accidental corruption. On boot, each candidate
registry is accepted only after the complete envelope is read from its slot,
its CRC and JFAPP descriptor are checked, resource digests are recomputed, and
the signed manifest is verified again against the configured key table. A
newer registry candidate whose envelope fails validation is skipped in favor
of an older valid candidate. The immutable lease copies the signer key id and
resource SHA-256 values only after this verification succeeds.

Registry format version 3 adds the envelope length and CRC. Existing version
1 and 2 registry records are refused; their bundle bytes are left untouched,
but the library is recovered as empty because the old records do not bind a
complete signed envelope. CRC metadata is never treated as an authentication
record. The ordinary parser unit tests use a fake signature callback. A separate
host qualification now compiles the actual adapter and parser against IDF5.3.1's
mbedTLS 3.6.0 software primitives: 6/6 cases, 289 checks pass, including valid
raw `r || s`, byte mutations, wrong keys, invalid scalars, domain NUL and
transactional parser rejection. Test keys enter only through a test-build
function; the production allowlist remains empty and rejects them. See
[qualification](../tests/package_signature/README.md) for exact scope and source
identity. ESP hardware crypto, positive install/reboot/tamper and registry
fallback probes remain unrun. Product keys still require reviewed provisioning.

Last updated: 2026-11-17; Applies to: 0.6.0-dev.
