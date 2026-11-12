# Runtime shared inline prepared-text v2 qualification

Last updated: 2026-11-12; Applies to: optional Core 0.9/ABI4 evaluation.

Canonical Runtime prepared-text admission accepts Core packet capability 1 or 2.
The separate legacy JFTP3 API gate is unchanged. Core owns decoding, signed Q1000
initial-X validation and glyph paint; Runtime retains the immutable packet bytes
through frame9/10 transport and accepted-frame ownership. Header/record widths
remain 32/52 bytes. Production Core0.8/ABI3 dependency defaults are unchanged.

The installed consumer has an opt-in REQUIRE_SHARED_INLINE_API configure probe.
New-Core qualification enables it, preventing a green result from skipped v2
cases. The old packet-v1 package still builds and passes the existing three
checks, and enabling the required-v2 probe against it fails configuration.

Frozen Core commit: 82ad1dcc98394d5568d51b6665ae92e629831e55.
Installed source identity:
8eb12e841028a0d56ee5eac1b4d8789f55787d784e2a046b4cc7ffc5fed0c0b8
(163 files).
Clang22 Release full and cropped installed consumers each pass 3/3 CTest checks
with R3 events and Node identity enabled. Full profile is render-core-default;
cropped is render-core-minimal-no-forms-advanced. The consumers stage Runtime
sources only; all Core headers and linkage come from the installed packages.

The codec test requires genuine packet-v2 bytes for both negative and positive
fractional initial X, runs frame9 and frame10 encode/decode and byte-identical
re-encode, checks unknown-version atomic refusal, and compares replay to direct
Core pixels. Accepted replay runs twice after supervisor replacement and host
catalog release; packet bytes remain unchanged. The worker test compares flat
and nested centered narrow paragraphs, requires genuine nonzero-origin v2,
destroys the worker and replaces supervisor ownership before replay, and checks
visible identical pixels with the accepted frame retaining its catalog.

Evidence directory: E:/Experiments/shared-inline-runtime-20261112.
Installed prefixes: install-full and install-cropped. Build trees:
E:/Builds/shared-inline-runtime-20261112-{full,cropped,v1}.
Logs: core-{full,cropped}-{configure,build,install}.log,
{full,cropped,v1}-{build,ctest}.log and v1-required-api-refusal.log.
The old-v1 source identity is
97aa69552d4e5e32b4996748330b7fea7312295f307b299129638931a4c2449d.

Reproduce new-package Runtime checks with:

```powershell
& E:/Experiments/shared-inline-runtime-20261112/qualify-runtime.ps1 -Mode full
& E:/Experiments/shared-inline-runtime-20261112/qualify-runtime.ps1 -Mode cropped
```

Clang22 ASan+UBSan full installed qualification also passes 3/3 with assertions,
R3 and Node identity enabled. Core, Runtime and Jerry core/ext are instrumented;
the existing Jerry platform port is uninstrumented. The same signed codec and
worker v2 positive assertions run. Logs are core-sanitizer-{configure,build,install}.log
and sanitizer-{configure,build,ctest}.log. Reproduce with install-core.ps1 then
qualify-runtime.ps1, each with -Mode sanitizer. The Core install script also
accepts full/cropped. All three packages use the final frozen identity above.

No flash or device execution is included in this host qualification.
