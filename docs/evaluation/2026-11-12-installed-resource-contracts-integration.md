# Installed font and stylesheet resource integration

Last updated: 2026-11-12; Applies to: 0.6.0-dev

Runtime19c12c92 is the base. Integrated font commitc6fc9a6b becomesae5e3a9f,
stylesheet commite72f131a becomes2b294a80, and10a332d1 retains the entry path
through worker initialization. Shared resources.h includes both DOM and bundle
metadata contracts. The two feature checkpoints document their precise admission
and retained-resource behavior.

The integrated source passes four focused Release host checks against Core
3468c310 with R3 and Node identity enabled. The checks cover resource references,
actual worker/owner BMP replay, named app-font selection/painting, ordinary app
runtime and device protocol. Independent feature qualification also covers the
locked Core0.8/ABI3 route and focused sanitizers. Host build
E:/Builds/installed-resource-contracts-host-20261112 and raw evidence under
E:/Experiments/installed-resource-contracts-20261112.

Full ESP-IDF5.3.1 build links Runtime10a332d1 and Core3468c310 with actual R3,
Node identity, CoreText, BMP, forms and frame10 enabled, RTTI disabled. It produces
0.6.2-abi4-resources2, binary size0x6a5290 (6,967,952 bytes), leaving17% of the
8MiB app partition free. SHA256
e28b1ec1436133921d684fbb7d9aba5df960e9a32d0b97bc757963f1a2d4350d.
The first complete target build exposed the missing session entry_path used by
the new preparation callback. Commit10a332d1 adds owned storage and initializes
it before task creation. Successful target logs cover the corrected build.

The current physical board remains resource1 with the restored simple acceptance
app, generation197. Resources2 physical acceptance is pending. The production
dependency remains Core0.8/ABI3/frame5. Core PR30 merged7c3c375b after CI3/3,
with head3468c310 matching this integration's Core code.

Font admission reuses the packaged fonts[] source/family metadata and validated
JFFONT2 metrics/weights. It allows eight declarations within the existing
16-resource,48KiB per resource and96KiB aggregate snapshot bounds. Source aliases
retain shared bytes. CSS preparation covers supported literal background URLs
in linked and embedded authored styles at launch, preserving their source base.
Recursive imports and dynamically authored relative URLs remain deferred.
