# JerryScript repair and IDF dependency delivery

Last updated: 2026-11-11; Applies to: 0.6.0-dev

This slice starts at Runtime master `161adce8`. It extracts the reviewed
JerryScript WEAK_REFS initialized-publication patch, applies it before all four
desktop CI engine builds, and builds an overlay of three files for ESP-IDF.
External engine sources are not modified. Unknown or partial patch inputs fail;
original, already-patched and CRLF inputs are accepted and verified by hashes.
The engine remains pinned at `b7069350c2e52e7dc721dfb75f067147bd79b39b`.

Two small IDF prerequisites are included: declare component requirements during
early expansion (including an empty non-script Jerry component), and remove a
three-line assignment to the already-removed frame renderer text_system option.
They correspond to prior experimental commits `29f17767` and `ae6f508f`.
No Event owner, Node identity, external Jerry context, frame10 resource owner or
production dependency adoption is included.

## Fresh delivery-branch checks

The released Core 0.8.0 archive matches SHA-256
`b650866d6fcf453e6b2cb54b3a685bbe9df1be07dd9d9825060651ec1e6c4240`.
It was built and installed separately. Runtime's MSVC Release package-provider
build passes complete CTest 20/20, including script and task worker tests, using
the patched MSVC Jerry libraries. Its generated provenance verifies ABI3,
source `fa5914ce1559da5b12522fa28775287a535543d1be64da19a985e3d3f84cd795`
/ 111, and `lockEnforced=true`. This build disables Core's own tests.

Fresh ESP-IDF 5.3.1 script and non-script builds both compile and link using
the independent Core R3 source `fdf9a7b0e04da9dc730642db078c6bca6f3143ef684e33f6175d15f854cf57f8`
/ 145, ABI4, profile `render-core-no-forms-advanced`. R3 script behavior is
not enabled by selecting that Core: this branch retains the existing bridge.
Forms are disabled for the legacy bridge's no-RTTI Xtensa build.

| Target | BIN bytes | SHA-256 |
| --- | ---: | --- |
| Script/task acceptance build | 5,923,376 | `1ead62378b4af0e2e54092b4ced7f6df060ddbf1b4b7b7d4a7cb91dded9bcef8` |
| Non-script build | 5,295,472 | `99c229f0f4add262a7a8fa4930de55d3830199a972553b5dd98b0ae3bf955117` |

Script uses the baseline static Jerry heap; the size report leaves only 6,181
DIRAM bytes before dynamic allocation. These are compilation results, not
device heap/stack or workload admission. Neither image was flashed. A preliminary
attempt pairing this port with Core 0.8 failed on existing ABI4 image/command
surfaces and the obsolete renderer option. That failed attempt is retained and
does not qualify ABI3 target support; ABI3 qualification above is desktop only.

Builds are `E:/Builds/runtime-weakref-delivery-*`; raw logs, released source and
provenance are under `E:/Experiments/runtime-weakref-delivery-20261111`.
Engine-only GC controls and original Node-candidate ASan/native tests remain
separately scoped in `2026-11-11-weakref-publication-fix.md`. The dependency
patch and its three postimage hashes are unchanged from those reviewed tests.
Remote CI remains the next delivery gate.
