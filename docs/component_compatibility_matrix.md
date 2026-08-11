# Component Compatibility Matrix

> Last updated: 2026-08-11; Applies to: 0.6.0-dev

This matrix records compatibility evidence across the three planned product
boundaries. It is intentionally narrower than the HTML/CSS capability tables:
those tables describe app-visible behavior, while this document describes which
build artifact may consume which other artifact.

## Current Matrix

| Consumer | Provider | Version / ABI | Status | Evidence and limitation |
| --- | --- | --- | --- | --- |
| JellyFrame App Runtime | in-tree `jellyframe_render_core` | same checkout | `verified` | Default desktop Release/Debug and non-scripting CI CTest. Use for synchronized Core/Runtime changes. |
| JellyFrame App Runtime | Core source override | local checkout / source profile | `verified locally` | `JELLYFRAME_RENDER_CORE_SOURCE_DIR` selects another Core checkout for cross-repository development; package mode remains mutually exclusive. |
| Render Core standalone tests | no Runtime or JerryScript | `0.6.0` / ABI `1` | `verified` | Standalone configure, build, CTest and install path. The package contains the Core target, headers and capability profile only. |
| JellyFrame App Runtime | installed Render Core package | `0.6.0` / ABI `1` | `verified` | Runtime uses `JELLYFRAME_RENDER_CORE_PROVIDER=package`; local consumer CTest is `7/7`, and the package-consumer CI job passed on `a934846`. |
| JellyFrame App Runtime | installed Render Core package | wrong version or ABI | `rejected` | Configure-time exact version and engine-ABI checks. No fallback to source Core is allowed in package mode. |
| App package preflight | generated Render Core capability profile | schema `1` / engine ABI `1` | `verified` | `package_app.py` validates profile schema, known feature IDs and dependency closure before resources are read; missing required families reject the package. |
| JellyFrame Script bridge | in-tree Render Core | `0.6.0-dev` source line | `verified separately` | JerryScript is optional and remains an App Runtime dependency. This does not prove a package-mode scripting build. |
| Device Runtime / launcher | Render Core package | port-selected | `port-owned` | Requires a port-owned toolchain, memory profile, panel path and hardware report. Desktop package evidence is not a device claim. |
| Ordinary `.jfapp` | native Render Core module | any | `unsupported by design` | App packages carry resources and declared scripts; they cannot load arbitrary executable native modules. |

## Locked Consumer Contract

The Runtime package consumer reads these values from
`cmake/jellyframe_dependency_lock.cmake`:

```text
JELLYFRAME_RENDER_CORE_LOCKED_VERSION   = 0.6.0
JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI = 1
```

The lock is a consumer policy, not a claim that every future Render Core build
must use the same version. A Core release may advance independently, but the
Runtime must update the lock, run the package-consumer build, and review the
capability profile before accepting it.

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
2. Keep the installed-package consumer in CI and record package provenance in
   Runtime and port build reports.
3. Publish a compatibility matrix for the first extracted Core repository.
4. Move Device Runtime contracts and official board delivery into the future
   JellyFrameOS boundary only after their host/port ownership contracts are
   stable.
