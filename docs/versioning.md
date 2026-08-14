# Versioning

> Last updated: 2026-08-14; Applies to: 0.6.0-dev

JellyFrame Engine uses a lightweight semantic versioning scheme:

```text
MAJOR.MINOR.PATCH[-stage]
```

## Rules

- Before `1.0`, `0.y.0` is a new active contract line and may intentionally
  contain incompatible runtime or public API changes.
- Before `1.0`, `0.y.z` is normally a focused fix, but may remove an incorrect
  or harmful pre-release contract when that reduces duplicate ownership or
  maintenance cost.
- `-dev`: mutable active development, not a compatibility target.
- Starting at `1.0`, `MAJOR.MINOR.PATCH` follows the stable public-contract
  policy: incompatible changes require a major version and an explicit
  migration decision where users need one.

See `pre_1_0_evolution_policy.md` for the current rule that architecture
clarity takes precedence over compatibility with pre-release artifacts.

Render Core packages add one compatibility dimension without changing the
repository version rule:

- The package version follows the Render Core release version.
- The engine ABI is a separate integer and changes only when the exported Core
  target contract is incompatible.
- JellyFrame Runtime pins both values in
  `cmake/jellyframe_dependency_lock.cmake`; a package with the wrong version or
  ABI is rejected at configure time.
- Device OS, device protocol and port versions are separate contracts. They are
  not inferred from a Render Core package version.

## Product Version Streams

The planned repositories do not share one release number:

| Stream | Example | Contract owner |
| --- | --- | --- |
| Render Core | `0.6.x-dev` | Core API/ABI, feature profile schema and renderer behavior |
| JellyFrame Runtime | `0.6.x-dev` | Japp format, App Runtime and JerryScript binding |
| JellyFrame Device OS | `0.1.x-dev` | launcher, registry, device lifecycle, images and ports |
| JFDP | `JFDP/1` | device control framing and result-code compatibility |

The current manifest schema requires `runtime.minJellyFrame`. A separate
`runtime.minRenderCore` requirement is a planned contract for the first
independently released Core package; it must not be hand-added to current apps
until the schema, packer, registry summary and runtime gate are implemented
together.

## Release Expectations

- The current source version is recorded in `VERSION`.
- User-visible changes are summarized in `CHANGELOG.md` and
  `CHANGELOG_zh.md`.
- Release CTest must stay meaningful: test binaries explicitly undefine
  `NDEBUG` and fail the build if `assert(...)` is disabled. CI also runs a
  Debug CTest pass. Linux CI additionally runs non-scripting core/tool coverage
  with AddressSanitizer and UndefinedBehaviorSanitizer; the optional
  JerryScript bridge needs its own compatible sanitizer toolchain before its
  sanitizer gate can be considered closed. Local Windows Clang sanitizer runs
  use `RelWithDebInfo`: the Debug CRT is not compatible with the dynamic ASan
  runtime, while CMake stages that runtime beside the sanitizer test binaries.
- Public documentation is provided in English and Chinese. Chinese files use a
  `_zh` suffix.
- Public Markdown documents carry a short freshness line near the top:
  `Last updated: YYYY-MM-DD; Applies to: VERSION`. Update it whenever the
  document's contract, examples or instructions change. During a new `-dev`
  cycle, unchanged documents may retain the last released version until their
  next substantive review; the date and version make that state visible.
  Generated support tables instead carry a `Source audit` line. CTest rejects
  first-party Markdown that omits a well-formed freshness/version marker.
- Licensing terms are described by `LICENSE`, `COMMERCIAL.md` and the README
  licensing section.
- Early releases are expected to stay small and milestone-based.

## Early project version map

- `0.1.x`: static HTML/CSS document core.
- `0.2.x`: framebuffer renderer and input routing.
- `0.3.x`: wearable app runtime development, including optional JerryScript,
  DOM mutation APIs, packaging, text/font workflow and embedded memory work.
- `0.4.x`: app-runtime stabilization for installable packages, pipeline
  diagnostics, responsive target reports, bounded animation, host service
  policy, font-family selection and Win32 validation tooling.
- `0.5.x`: device-usability work: storage lifecycle integration, retained
  rendering slices, production image codec adapters, system shell recovery and
  broader real-device validation.
- `0.6.x`: external developer trial. It starts only after 0.5 closes its
  device-usability, diagnostic and host-contract gates; its focus is trial
  feedback, distribution semantics and target-device evidence, not broad
  browser compatibility.
- `0.7.x` through `0.9.x`: converge independent Core/Runtime/Device OS
  boundaries, remove remaining transitional ownership and prepare the public
  contract. Breaking cleanup remains allowed when it is documented and fully
  tested.
- `1.0`: first stable public-contract release. Manifest, capability, target
  gate, diagnostic-code and host-service error semantics gain compatibility and
  deliberate migration requirements.
