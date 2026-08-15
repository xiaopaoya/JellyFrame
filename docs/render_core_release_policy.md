# Render Core Release And Extraction Policy

> Last updated: 2026-08-15; Applies to: 0.6.0-dev

This policy governs the transition from the current monorepo boundary to an independently governed `jellyframe-render-core` project. It complements [engine_architecture.md](engine_architecture.md); it is not a user-facing app compatibility promise.

## Ownership

| Project | Owns | Must not own |
| --- | --- | --- |
| `jellyframe-render-core` | HTML/CSS/DOM, layout, paint, input semantics, feature families and Core profile schema | app installation, JerryScript, filesystems, device transports, board drivers, launcher policy |
| `jellyframe` | Japp format, App Runtime, JerryScript binding, desktop shell and author tools | board image policy or private Render Core implementation headers |
| `jellyframe-device-os` | launcher, registry, JFDP transport, official images and ports | Render Core semantics or private Runtime implementation |

## Release Unit

A Render Core release contains:

1. Core sources, public headers, CMake package export and standalone tests.
2. A versioned feature registry/profile schema and generated capability profile.
3. A source manifest and SHA-256 artifact checksum.
4. An annotated, signed release tag and published release artifacts.

The tag signature establishes release authority. The source-manifest/checksum
establishes artifact identity; neither replaces the other. The current
deterministic archive is the precursor to this release unit.

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

JellyFrame Runtime records the exact Render Core version, ABI and release
artifact SHA-256 in its dependency lock. A dependency update is an explicit
reviewed change that runs:

1. Runtime tests against the installed Core package.
2. Runtime tests against a local source override of that Core revision.
3. Core standalone build/install/test from the published source artifact.

Device OS pins a JellyFrame Runtime release and a named board feature profile.
It does not infer supported features from a branch name, and it must record the
consumed Core provenance in every image acceptance report.

A lock rollback is an explicit dependency change: restore the previously
verified exact version, ABI and artifact hash, then repeat the package-consumer
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

The first Core repository is produced with a reproducible `git filter-repo`
export that retains the history of `src/render_core`, its Core CMake boundary,
standalone tests and Core-specific documents. The Runtime repository keeps its
product history and replaces in-tree Core use with a pinned package-consumer
commit. A history-free directory copy is not an acceptable extraction.

Before publishing the new repository, verify:

1. A clean clone builds, tests and installs Core without Runtime, JerryScript,
   ports or sample apps.
2. A clean Runtime clone consumes the published Core package through its lock.
3. A local Runtime checkout can use the documented source override.
4. A Device OS profile build records the exact Runtime/Core provenance.

The project does not use Git submodules for ordinary development. Locks plus
published packages are the release dependency mechanism.

## Extraction Gate

Physical extraction is allowed only when all four verification paths are green
for one release candidate and no private Runtime/port include enters Core. The
first high-value Core capability pack must be developed on this governed
boundary, either immediately after extraction or in the same release window;
large new CSS work must not accumulate in the transitional monorepo.
