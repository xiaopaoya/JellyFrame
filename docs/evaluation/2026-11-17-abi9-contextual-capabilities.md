# ABI9 contextual capability and provider checkpoint

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

## Status

JFDP image identity, the host provider, install inspection and local provider
packaging now distinguish the ABI9 contextual DOM route from ABI4 shaping.
The actual ESP-IDF5.3.1 target's complete 13-family profile matches the wire
capability helper. A local provider archive built from that target passes
profile, capacity, provenance and isolated-import checks. These are host and
target-build results, not signed execution, physical transport or publication.

This advances the capability gate left open by
[the installed-port checkpoint](2026-11-17-abi9-contextual-port.md).
Runtime parent is `d677c125`; Core remains unchanged at
`f35f1f778119f360d508b69b0c7a0eaeafae6cb4`, candidate0.6.2/ABI9,
source SHA-256 `997ace75be360ad2f5c3afcac3f21eb92bf3e605b670bb00fac23d7e75beed61`
(640 files). Exact checkpoint receipts and reproduction scripts are under
`E:/Experiments/migration-contextual-capabilities-20261117`.

## Contract

Existing JFDP bits0..6 and payload layout are unchanged. Appended bits7/8
identify `text.contextual-jfsp2` / `text.contextual-layout`; bits9..13 encode
the actual Core profile's length math, counters, borders, tables and
positioned sizing. Unknown bits and invalid ABI/dependency combinations
refuse in both C++ wire encoding/decoding and Python identity validation.
Document and paint are both mandatory, correcting an earlier Python check
that accepted either base bit alone.

The shared host capability module derives explicit installed routes:

| Matched identity | Allowed shaped install route |
|---|---|
| ABI4 with `text.shaping` | `jfsp1-block-v11` |
| ABI9 with both contextual families | `jfsp2-block-v12` |
| ABI9 contextual JFSP2 without contextual layout | None |
| Scalar identity | None |

Conflicting families or ABI declarations refuse rather than granting another
route. Historical ABI4's `jfsp1-block-v11` also carries JFSP2 packet version2;
packet magic alone does not identify an installed layout route.

The physical provider derives the route only after its existing complete
manifest/live-image attestation. Package inspection requires the exact
declared route and a signed envelope before INSTALL_BEGIN. It reports the
pipeline kind and leaves `signatureVerified` false: device verification is
still authoritative. Inspection no longer accepts a generic `allow_shaped`
boolean. Byte ownership through install remains immutable even if the input
file is replaced after inspection.

The release packager validates complete families instead of dropping unknown
ones. Text-enabled releases require actual IDF build/capacity evidence. The
provider and author SDK both bundle the shared capability module, with
isolated copied-tool imports tested. Production defaults, dependency lock,
keys and historical ABI4 fixture route are unchanged.

## Verification

| Gate | Result |
|---|---|
| Provider/tool regression discovery | 59/59 |
| Device-image manifest regression | 5/5 |
| Signing regression | 5/5 |
| ABI9 source ON / cropped / OFF | 35/35 each, no skips |
| ABI4 source compatibility | 36/36, no skips |
| ESP-IDF5.3.1 installed-worker target | Built successfully |
| Complete target family / helper wire / provider comparison | 13 families, mask `0x3fbf`, exact match |
| Actual target local provider archive | ABI9, 2093056B per slot, isolated imports pass |

Wire checks cover old/new round trips, incompatible ABI, missing contextual
dependency and unknown bits. Host checks cover exact route grants/refusals,
pre-transfer refusal, immutable bytes, typed identities, C++/Python bit parity
and ABI4/ABI9 archive packaging. An earlier cancellation timing test failed
under concurrent build load; reruns passed. That existing timeout was not
changed or claimed repaired. Full native SDK packaging was not performed;
the SDK result here is specifically copied author-tool import qualification.

`audit-capabilities.py` uses the recorded target compiler command to extract
the lifecycle translation unit's actual macros. It compiles the production
capability helper with those macros and the real C++ protocol encoder, then
decodes with the provider and compares the whole generated profile. Identity
strings in this host helper are fixtures; this is explicitly not a physical
wire capture. The committed target checkpoint supplies actual image identities.

`package-target.py` creates a local offset-zero factory image from the exact
recorded flash inputs, then invokes the real release packager. An initial
draft was rejected because SDKCONFIG_DEFAULTS lacked a named board profile.
The same unchanged config bytes were given a named WS147 contextual profile;
no validation was weakened. Generated sdkconfig SHA-256 remains
`ac782e1cc0787610364618e670488958c62a01619a90e3886f2a1eb902ad3e72`.

The 6MiB store profile changes partition boundaries. Neither this archive nor
the successful build authorizes an app-only deployment. GCC's existing Core
misleading-indentation warnings remain visible with the previously recorded
Core-component Werror exemption. No serial enumeration or flashing occurred.

## Remaining gates

Complete installed OFF/full/cropped candidate-manifest qualification remains
open. Then qualify actual signed ABI9 full-resource store-to-Binding-to-worker
painting, refusal/recovery, Runtime heap/reserve/latency and broader product
route parity. Exact physical signed install/reboot/tamper/fallback/display and
touch require matched trust/profile/data migration. Review, hosted CI,
publication and enablement remain separate. Closed ABI4 generation200 device
acceptance remains closed; full experimental migration is incomplete.
