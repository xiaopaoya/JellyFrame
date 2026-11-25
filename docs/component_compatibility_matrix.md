# Component Compatibility Matrix

> Last updated: 2026-11-25; Applies to: 0.6.0-dev.

This matrix records compatibility evidence across the three planned product
boundaries. It is intentionally narrower than the HTML/CSS capability tables:
those tables describe app-visible behavior, while this document describes which
build artifact may consume which other artifact.

## Current Matrix

| Consumer | Provider | Version / ABI | Status | Evidence and limitation |
| --- | --- | --- | --- | --- |
| JellyFrame App Runtime | in-tree `jellyframe_render_core` | same checkout | `verified` | Default desktop Release/Debug and non-scripting CI CTest. Use for synchronized Core/Runtime changes. |
| JellyFrame App Runtime | unpacked Core source archive | `0.13.0` / ABI `11` | `verified` | The source override passed 14/14 focused regressions. `JELLYFRAME_RENDER_CORE_SOURCE_DIR` is mutually exclusive with package mode. |
| Render Core standalone tests | no Runtime or JerryScript | `0.13.0` / ABI `11` | `verified` | The released archive passed full, cropped and disabled-feature qualification with 46/46, 45/45 and 42/42 tests. |
| Render Core source archive | no Runtime or JerryScript | `0.8.0` / ABI `3` | `released` | Signed `v0.8.0` source archive; SHA-256 `b650866d6fcf453e6b2cb54b3a685bbe9df1be07dd9d9825060651ec1e6c4240`. |
| Render Core source archive | no Runtime or JerryScript | `0.9.0` / ABI `4` | `released` | Signed `v0.9.0`; adopted by Runtime #116. The [ABI4 integration plan](render_core_abi4_migration_plan.md) records native and installed-worker WS147 acceptance. |
| Render Core source archive | no Runtime or JerryScript | `0.10.0` / ABI `9` | `released` | Signed `v0.10.0` release with a source archive and SHA-256 sidecar. |
| Render Core source archive | no Runtime or JerryScript | `0.13.0` / ABI `11` | `released` | Signed [`v0.13.0`](https://github.com/xiaopaoya/JellyFrame-Render-Core/releases/tag/v0.13.0), deterministic source archive SHA-256 `ec1773ad7d35b88f5e84903259433a6e5ceb922b6a391caec2cdcf2c453cf1f9`. |
| Render Core source archive | no Runtime or JerryScript | `0.6.2` / ABI `1` | `released` | Signed `v0.6.2` release archive plus SHA-256 sidecar. Declared text members are normalized to LF while binary members remain opaque; CI proves equal archive bytes from equivalent CRLF/LF trees, then extracts, builds, runs CTest, installs and configures the Runtime package consumer. |
| JellyFrame App Runtime | installed Render Core package | `0.9.0` / ABI `4` | `verified` | Historical production lock from #116 through #127. The ABI4 CI lane retains its own exact lock. |
| JellyFrame App Runtime | installed Render Core package | `0.10.0` / ABI `9` | `verified (host)` | Historical lock adopted in #128. [Host qualification](abi9-paired-qualification.md) covers full, cropped and off for the recorded ABI9 pair. |
| JellyFrame App Runtime | installed Render Core package | `0.12.0` / ABI `11` | `verified (host)` | Historical lock adopted in [PR #143](https://github.com/xiaopaoya/JellyFrame/pull/143). The package passed focused consumer, desktop and scripting tests. |
| JellyFrame App Runtime | installed Render Core package | `0.12.1` / ABI `11` | `verified (host)` | Earlier production lock. |
| JellyFrame App Runtime | installed Render Core package | `0.13.0` / ABI `11` | `verified (host)` | Previous production lock, adopted in [PR #159](https://github.com/xiaopaoya/JellyFrame/pull/159). Runtime package consumption passed 41/41 tests for full, cropped and disabled-feature packages; post-merge CI passed 26/26 jobs. |
| JellyFrame App Runtime | installed Render Core package | `0.14.0` / ABI `11` | `verified (host)` | Full, cropped and off pass 41 installed tests each. The signed scalar route passes 9 tests each. The signed shaped route passes 22 each on full and cropped. |
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
JELLYFRAME_RENDER_CORE_LOCKED_VERSION = 0.14.0
JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI = 11
JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH = 398f427102fdc9c4694025a65c07ee5430a083e72fb563449c10571e9046f480
```

The lock is consumer policy. Core can publish independently; Runtime adopts a
release through a reviewed lock update, installed-package builds and capability
profile review. The signed
[`v0.14.0` release](https://github.com/xiaopaoya/JellyFrame-Render-Core/releases/tag/v0.14.0)
has archive SHA-256 `dd186659b5aedc184291cb82a5dfb7da87d21b8a64d573103622298838aa3d4b`.
The archive checksum is recorded separately from the source manifest; an
installed CMake package exposes the source identity only.

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
