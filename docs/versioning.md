# Versioning

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
- Public documentation is provided in English and Chinese. Chinese files use a
  `_zh` suffix.
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
