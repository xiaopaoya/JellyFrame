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

## Release discipline

- Keep the current version in `VERSION`.
- Update both `CHANGELOG.md` and `CHANGELOG_zh.md` for user-visible changes.
- Public documentation should be maintained in English and Chinese. The Chinese
  file uses a `_zh` suffix.
- Licensing changes must update `LICENSE`, `COMMERCIAL.md`, both README files
  and both changelog files in the same release batch.
- Prefer small, milestone-based releases over large untracked batches.

## Early project version map

- `0.1.x`: static HTML/CSS document core.
- `0.2.x`: framebuffer renderer and input routing.
- `0.3.x`: wearable app runtime development, including optional JerryScript,
  DOM mutation APIs, packaging, text/font workflow and embedded memory work.
- `0.4.x`: future stabilization of packaged app APIs after more real-device
  validation.
