# Component Compatibility Matrix

> Last updated: 2026-11-14; Applies to: 0.6.0-dev; Core 0.9.0-dev qualification / Runtime 0.8.0 production lock

This matrix records compatibility evidence across the three planned product
boundaries. It is intentionally narrower than the HTML/CSS capability tables:
those tables describe app-visible behavior, while this document describes which
build artifact may consume which other artifact.

## Current Matrix

| Consumer | Provider | Version / ABI | Status | Evidence and limitation |
| --- | --- | --- | --- | --- |
| JellyFrame App Runtime | in-tree `jellyframe_render_core` | same checkout | `verified` | Default desktop Release/Debug and non-scripting CI CTest. Use for synchronized Core/Runtime changes. |
| JellyFrame App Runtime | unpacked Core source archive | `0.6.2` source profile | `verified` | `JELLYFRAME_RENDER_CORE_SOURCE_DIR` selects the unpacked archive for a separate Runtime build and App Runtime CTest; package mode remains mutually exclusive. |
| Render Core standalone tests | no Runtime or JerryScript | `0.6.2` / ABI `1` | `verified` | Standalone configure, build, CTest and install path. The package contains the Core target, headers and capability profile only. |
| Render Core source archive | no Runtime or JerryScript | `0.8.0` / ABI `3` | `released` | Signed `v0.8.0` source archive; SHA-256 `b650866d6fcf453e6b2cb54b3a685bbe9df1be07dd9d9825060651ec1e6c4240`. |
| Render Core independent `master` | no Runtime or JerryScript | `0.9.0-dev` / ABI `4` | `qualification track` | Current master `de9733d2`; the implementation revision qualified by Runtime PR #71 is `b0bfd834`, with package identity `aa908e38…d18c4` (165 implementation inputs). The later Core change adds only shaping-closure tooling, tests and docs. The matrix covers worker frame v9/v10 generated-text/replay, shared-inline `counter(list-num)`, and native decimal/disc list markers. No signed ABI4 package or Device OS image acceptance exists. |
| Render Core source archive | no Runtime or JerryScript | `0.6.2` / ABI `1` | `released` | Signed `v0.6.2` release archive plus SHA-256 sidecar. Declared text members are normalized to LF while binary members remain opaque; CI proves equal archive bytes from equivalent CRLF/LF trees, then extracts, builds, runs CTest, installs and configures the Runtime package consumer. |
| JellyFrame App Runtime | installed Render Core package | `0.8.0` / ABI `3` / normalized locked source manifest identity | `verified` | Runtime uses `JELLYFRAME_RENDER_CORE_PROVIDER=package`; it validates the package manifest against the exact locked source hash and copies it into build provenance. The identity normalizes text line endings, so Windows and Unix checkouts of the same sources resolve identically. |
| JellyFrame App Runtime | installed Render Core package | wrong version, ABI or source identity | `rejected` | Configure-time exact version, engine-ABI and source-hash checks. No fallback to source Core is allowed in package mode. |
| App package preflight | generated Render Core capability profile | schema `1` / selected Core engine ABI | `verified` | `package_app.py` validates profile schema, known feature IDs and dependency closure before resources are read; missing required families reject the package. |
| JellyFrame Script bridge | in-tree Render Core | `0.6.0-dev` source line | `verified separately` | One script backend is selected at build time; the current implementation is JerryScript and remains an optional App Runtime dependency. This does not prove a package-mode scripting build. |
| App Runtime / future Device OS host | `jellyframe_device_runtime_contracts` | `JFDP/1` | `verified` | The target independently tests framing, typed payloads and staging, without an App Runtime or Render Core implementation dependency. Its monorepo location is transitional; this is not a Device OS release. |
| Device Runtime / launcher | Render Core package | port-selected | `port-owned` | Requires a port-owned toolchain, memory profile, panel path and hardware report. Desktop package evidence is not a device claim. |
| Ordinary `.jfapp` | native Render Core module | any | `unsupported by design` | App packages carry resources and declared scripts; they cannot load arbitrary executable native modules. |

## Locked Consumer Contract

The Runtime package consumer reads these values from
`cmake/jellyframe_dependency_lock.cmake`:

```text
JELLYFRAME_RENDER_CORE_LOCKED_VERSION   = 0.8.0
JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI = 3
JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH = fa5914ce1559da5b12522fa28775287a535543d1be64da19a985e3d3f84cd795
```

The lock is a consumer policy, not a claim that every future Render Core build
must use the same version. A Core release may advance independently, but the
Runtime must update the lock, run the package-consumer build, and review the
capability profile before accepting it.

The first independent release was `v0.6.0`. The independent development head
is `0.9.0-dev` / ABI 4 and remains a qualification track. Runtime package mode
accepts signed
[`v0.8.0`](https://github.com/xiaopaoya/JellyFrame-Render-Core/releases/tag/v0.8.0).
Its deterministic source archive SHA-256 is
`b650866d6fcf453e6b2cb54b3a685bbe9df1be07dd9d9825060651ec1e6c4240`.
This release-artifact identity is recorded separately from the source manifest
because an installed CMake package cannot prove which archive produced it.

Each configure writes `generated/jellyframe_render_core_provenance.json` next
to the copied or generated profile. It is the portable record to archive with
Runtime or port build evidence: provider, actual package version/ABI, profile
filename, consumer lock values and deterministic source hash/file count. An
installed package must also supply a matching source manifest, which package
mode validates, compares to the exact lock and copies next to the provenance
record. The hash is a content identity, not a substitute for a signed release
or reviewed lock.

## Evidence Rules

- `verified` means the named build boundary has a reproducible automated test.
- A package-consumer result is release evidence only when both the local CTest
  and the remote CI job pass for the same commit.
- `port-owned` means the core contract is available, but the result depends on
  board, panel, toolchain or RTOS behavior and must be reported by the port.
- `not-tested` must remain visible when no valid evidence exists; it must not be
  converted to `supported` from a desktop build.

## Planned Extraction Sequence

1. Keep the in-tree provider for synchronized Core/Runtime development.
2. Keep the deterministic source archive and installed-package consumer in CI;
   archive their generated provenance record with Runtime and port build reports.
3. Keep the compatibility matrix synchronized with the independent Core
   repository; development heads remain standalone-only until an explicit
   Runtime lock update and reviewed release artifact exist.
4. Move Device Runtime contracts and official board delivery into the future
   JellyFrameOS boundary only after their host/port ownership contracts are
   stable.
