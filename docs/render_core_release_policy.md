# Render Core Release And Extraction Policy

> Last updated: 2026-09-04; Applies to: 0.6.0-dev

This policy governs the transition from the current monorepo boundary to an independently governed `jellyframe-render-core` project. It complements [engine_architecture.md](engine_architecture.md); it is not a user-facing app compatibility promise.

## Ownership

| Project | Owns | Must not own |
| --- | --- | --- |
| `jellyframe-render-core` | HTML/CSS/DOM, layout, paint, input semantics, feature families and Core profile schema | app installation, JerryScript, filesystems, device transports, board drivers, launcher policy |
| `jellyframe` | Japp format, App Runtime, JerryScript binding, desktop shell and author tools | board image policy or private Render Core implementation headers |
| `jellyframe-device-os` | launcher, registry, JFDP transport, official images and ports | Render Core semantics or private Runtime implementation |

## Migration Status

The history-preserving `xiaopaoya/JellyFrame-Render-Core` repository retains
the existing license and contributor history unchanged. Its first signed
`v0.6.0` release established the deterministic source archive and SHA-256
sidecar; the Runtime currently accepts signed `v0.6.2`. Its `master` now contains
the reviewed `0.6.2-dev` synchronization from JellyFrame `7735b9a1` at merge
`769ec5d`; this development head is not a Runtime dependency. No license change,
contributor-policy change or open-source intent is implied. The Runtime
accepts a release only through an explicit dependency-lock update;
its in-tree provider remains the default for synchronized development only.
The Runtime CI downloads the published archive, verifies the reviewed
SHA-256, installs it and runs the locked package-consumer regression; this
prevents a monorepo export from being mistaken for the released dependency.

## Release Unit

A Render Core release contains:

1. Core sources, public headers, CMake package export and standalone tests.
2. A versioned feature registry/profile schema and generated capability profile.
3. A source manifest and SHA-256 artifact checksum.
4. An annotated, signed release tag and published release artifacts.

The tag signature establishes release authority. The source-manifest/checksum
establishes artifact identity; neither replaces the other. The current
deterministic archive is the precursor to this release unit. Its declared text
members are normalized to LF before packing, while opaque binary members stay
byte-for-byte; equivalent CRLF/LF checkouts therefore produce the same archive
bytes and checksum. When run from a Git checkout, the packer admits only
tracked Core inputs, so untracked editor or build artifacts cannot enter a
candidate archive. A signed release is still created from the reviewed tag.

## Versions And Compatibility

- Render Core begins independent publication at `0.6.0`, with Core ABI `1`.
- A patch release fixes behavior, robustness or performance without changing a
  documented public Core contract or profile schema.
- Before 1.0, a required incompatible Core API, feature-profile or rendering
  contract change uses a new minor line and an explicit release note. Incorrect
  pre-release contracts may be removed rather than preserved as aliases.
- ABI changes are independent of package version and increment only when the
  exported CMake/public-header binary contract is incompatible.
- JFDP follows `JFDP/<major>`; a breaking wire change increments the major.

## Consumer Locks

JellyFrame Runtime records the exact Render Core version, ABI and deterministic
source hash in its dependency lock. The published release metadata records the
release archive SHA-256 separately: an installed CMake package exposes a source
manifest but cannot prove its original archive bytes. A dependency update is an
explicit reviewed change that runs:

1. Runtime tests against the installed Core package.
2. Runtime tests against a local source override of that Core revision.
3. Core standalone build/install/test from the published source artifact.
4. Verification of the published archive SHA-256 before the installed-package
   consumer build.

Device OS pins a JellyFrame Runtime release and a named board feature profile.
It does not infer supported features from a branch name, and it must record the
consumed Core provenance in every image acceptance report.

A lock rollback is an explicit dependency change: restore the previously
verified exact version, ABI and source hash, then repeat the package-consumer
tests. No consumer may silently select a newer compatible-looking Core package
or use a floating branch as a rollback mechanism.

`JELLYFRAME_RENDER_CORE_SOURCE_DIR` remains a local, mutually exclusive
development override. It is not a public deployment dependency and does not
replace the locked package test.

## Profiles And Trimming

A Core release is source and package infrastructure, not one universal native
firmware image. Each Device OS image selects a build-time feature profile.
Unselected families are not linked; the emitted capability profile is the
authoritative runtime contract. App manifests negotiate required/optional
features before loading resources.

An ordinary `.jfapp` may carry only validated data resources. It may never load
a native Core feature module, shared object or arbitrary binary. A future
firmware feature pack, if justified, is host-signed, versioned and rolled back
as part of the Device OS image lifecycle rather than the app package.

## History-Preserving Extraction

The first Core repository was produced with a reproducible `git filter-repo`
export that retains the history of `src/render_core`, its Core CMake boundary,
standalone tests and Core-specific documents. The Runtime repository keeps its
product history and replaces in-tree Core use with a pinned package-consumer
commit. A history-free directory copy is not an acceptable extraction.

The completed Core/Runtime extraction and release paths are:

1. A clean clone builds, tests and installs Core without Runtime, JerryScript,
   ports or sample apps.
2. A clean Runtime clone consumes the published Core package through its lock.
3. A local Runtime checkout can use the documented source override.

Device OS migration has one additional, still-open gate: a Device OS profile
build must record the exact Runtime/Core provenance. The current desktop
dispatcher does not satisfy it.

The project does not use Git submodules for ordinary development. Locks plus
published packages are the release dependency mechanism.

## Rehearsal

To audit or repeat the history-preserving export, run the committed-HEAD-only rehearsal in
a disposable directory:

```powershell
python project_tools\rehearse_render_core_history_export.py `
  --output-dir build\render-core-history-export
```

It requires `git-filter-repo`, performs no mutation in the source checkout,
and rejects an export that loses Render Core history, lacks standalone entry
points, or retains Runtime/port paths. CI installs that tool, then configures,
builds, runs CTest and installs the filtered export. The rehearsal is evidence
for extraction readiness, not the
actual signed repository publication.

## Extraction Gate

Core physical extraction is complete: the first signed `v0.6.0` release, the
currently locked `v0.6.2` package and Runtime package-consumer CI close that
boundary. Before Device OS migration,
the remaining profile-consumer/provenance check must be green and no private
Runtime/port include may enter Core. The next high-value Core capability pack
must be developed on this governed boundary; large new CSS work must not return
to the transitional monorepo.
