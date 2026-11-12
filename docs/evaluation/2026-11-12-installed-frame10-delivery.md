# Installed frame10 delivery

Last updated: 2026-11-12; Applies to: 0.6.0-dev

The installed app retains one immutable resource snapshot, Core system-font
catalog and bounded BMP provider through supervisor, worker and UI handles.
Owner replacement admits resources before publication. Rejected replacement
preserves the previous owner. The session releases its retained snapshot after
teardown completes and outstanding rendering handles drain.

One admission header selects frame10 when canonical text APIs, Core Text and
BMP are all enabled. Every owner construction/bind and codec/session branch
uses that predicate. Other combinations begin the ordinary resource-free session.
The target links shared runtime image/font implementations in every script-task
configuration because canonical render code references their methods even when
installed resource adapters are disabled.

## Fresh qualification

MSVC Release Host complete CTest passes57/57 with R3 ON and57/57 with R3 OFF,
including the independent owner/resource tests and four flag-combination
translation units executing the shared session entry. Existing assertion and
sanitizer staging helpers apply to added test targets. Noncanonical owner
coverage returns an explicit skip while ordinary resource tests still execute.
The Host profile disables optional examples, benchmarks and sample regressions.
An initial default-tools build failed on existing legacy DisplayCommand field
access against opaque ABI4 commands. That log is retained in the evidence.

The actual locked Core0.8/ABI3 package passes27 registered Runtime checks,
including one explicit canonical-owner skip. Ordinary resource/session tests
execute. The initial owner target exposed unavailable new image types in the
old package. Its private header now includes the adapter only with canonical
capability, and a compile probe selects the corresponding test implementation.
New-Core positive resource/owner/session checks pass7/7 in each Event mode.
The CI R3 full job now builds and executes the owner and all four session
feature combinations against the pinned new Core source.

Clang ASan+UBSan owner/resource/four-session/combined script-worker tests pass
7/7 with assertions enabled. Runtime and Core C++ plus JerryScript core/ext
are instrumented, with the platform port library uninstrumented and leak
detection disabled. The canonical owner test executes positively in this run.

ESP-IDF5.3.1 installed lifecycle builds link in all four CoreText/BMP combinations.
Generated sdkconfig confirms each requested pair and lifecycle acceptance.

| CoreText/BMP | BIN bytes | SHA256 |
| --- | ---: | --- |
| OFF/OFF | 6,214,032 | `d833cd07f5d2be8b3914c523f906acae11c9ab8e797bceede1258407e427392d` |
| OFF/ON | 6,219,888 | `a144aedc71b5efd21ea4b92355971418a704a34188070f4eabe7fad7c314fbd4` |
| ON/OFF | 6,933,232 | `1c1425486d09a49a349a03e028afd1ca2fe055b75d21f3a8e28f88835833db3b` |
| ON/ON | 6,948,832 | `bb335bba404f92fde3eb2c05b666e43d93ccb77b58e11346e14d84405e207b37` |

All builds select Coref7aee11c, source
`f7c17009b7660a84ef6a492367637187dff6915c4af3418c769902b3b7eb2559` /145,
Core0.9.0-dev/ABI4 render-core-default. The first OFF/OFF target failed with a
missing RuntimeImageProvider painter symbol. Including its shared implementation
fixed the link, and the complete matrix above uses the correction.

Evidence and runners are under E:/Experiments/frame10-delivery-20261112.
Builds are E:/Builds/runtime-frame10-*. Image hashes identify these precise
build inputs. Device installation, page transitions, input, accepted-frame
lifetime and dynamic resource watermarks remain pending.

The catalog retains the firmware JFFONT2 system font at index zero. The installed
frame10 font route additionally reads the existing normalized JFAPPV0 summary's
`fonts[].source` and optional `family` through the existing bounded summary
parser. It snapshots every declared JFFONT2 in declaration order and registers
its normalized CSS family hash with Core. Multiple resources in one family
support different native weights; source aliases are allowed. The `sizes`,
`weights`, profile and license fields remain packaging/diagnostic metadata:
validated JFFONT2 bytes alone determine Core metrics, native weight and paint.

Font admission is bounded to eight declarations, 255 source bytes and 95 family
bytes, within the existing 4 KiB summary. Nine catalog faces (firmware plus eight
app declarations) fit the existing 64-face/16-family Core defaults. Declared
legacy JFFONT, missing or corrupt bytes, invalid paths/families, and exhausted
snapshot budgets refuse the entire candidate. App fonts share the existing
16-entry, 48 KiB per-resource and 96 KiB aggregate snapshot budgets. In
particular, large CJK font supplements exceeding 48 KiB are refused by this
bounded installed target. The firmware system font is outside those app budgets.

The font table and summary-read workspace are temporary heap allocations;
DeviceBundleSummary and DeviceBundleDescriptor retain their original size.
Storage leases remain supervisor-owned. Font catalog handles independently pin
the same immutable snapshot bytes after owner replacement and session teardown.
Refusal preserves the previous published owner. The ordinary/old-Core session
path skips optional font admission and retains its prior behavior.

Follow-up evidence is under E:/Experiments/installed-font-metadata-20261112.
MSVC Release new-Core focused tests cover named-family hash resolution,
regular/bold selection, exact Core metrics, prepared glyph paint, invalid
metadata/bytes/kind/limits, preserved output, and old-catalog paint after
replacement. Protocol tests exercise the same summary parser's optional font
reader. The actual locked Core 0.8/ABI3 executes ordinary resource and protocol
checks with an explicit canonical-owner skip.
Clang ASan+UBSan passes all three focused owner/resource/protocol tests with
assertions enabled; Runtime and Core are instrumented. The existing
instrumented JerryScript core/ext libraries are reused, the platform port is
uninstrumented, and leak detection is disabled as in the previous fixture.

Six changed translation units also compile with ESP-IDF 5.3.1 / Xtensa GCC
13.2 using the existing CoreText=ON/BMP=ON profile (profile `11`), with fresh
objects and `-fstack-usage` files outside the frozen build. The launch frame
changes from 1552 to 1616 bytes, frame dispatch 928 to 976 bytes and owner
creation 208 to 224 bytes. The new font reader uses 592 bytes. These are
compiler-local static frame sizes, not measured complete task high-water marks.
No task stack size was increased. This is object compilation, not a new full
firmware link or device qualification. Stylesheet-relative CSS extraction
remains a separate follow-up.
