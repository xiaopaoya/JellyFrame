# Versioning

> Last updated: 2026-07-23; Applies to: 0.5.0-dev

JellyFrame Engine uses a lightweight semantic versioning scheme:

```text
MAJOR.MINOR.PATCH[-stage]
```

## Rules

- `MAJOR`: incompatible runtime or public API changes.
- `MINOR`: new engine capabilities that remain compatible with existing apps.
- `PATCH`: bug fixes, parser/layout correctness fixes and documentation-only
  maintenance.
- `-dev`: active development before a stable tagged release.

## Release Expectations

- The current source version is recorded in `VERSION`.
- User-visible changes are summarized in `CHANGELOG.md` and
  `CHANGELOG_zh.md`.
- Release CTest must stay meaningful: test binaries explicitly undefine
  `NDEBUG` and fail the build if `assert(...)` is disabled. CI also runs a
  Debug CTest pass. Linux CI additionally runs non-scripting core/tool coverage
  with AddressSanitizer and UndefinedBehaviorSanitizer; the optional
  JerryScript bridge needs its own compatible sanitizer toolchain before its
  sanitizer gate can be considered closed.
- Public documentation is provided in English and Chinese. Chinese files use a
  `_zh` suffix.
- Public Markdown documents carry a short freshness line near the top:
  `Last updated: YYYY-MM-DD; Applies to: VERSION`. Update it whenever the
  document's contract, examples or instructions change. Documentation may lag a
  code patch briefly during active development, but stale files should be easy
  to spot from this line.
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
- `0.7.x` through `1.0`: public-contract freeze. Manifest, capability, target
  gate, diagnostic-code and host-service error semantics may change only with
  compatibility or an explicit migration path.
