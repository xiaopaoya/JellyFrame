# JellyFrame Render Core

> Last updated: 2026-08-14; Applies to: 0.6.0-dev

JellyFrame Render Core is a platform-neutral, modular HTML/CSS subset,
document/layout pipeline and CPU software renderer for bounded embedded and
desktop hosts. It does not include the JellyFrame App Runtime, JerryScript,
ports, launchers, device protocols or vendor SDKs.

## Build

With CMake 3.20+ and Ninja available, presets make the default or smallest
supported profile one command away:

```sh
cmake --preset default
cmake --build --preset default

cmake --preset minimal
cmake --build --preset minimal
```

The following explicit commands remain compatible with the minimum required
CMake version and are useful when a host selects its own generator:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix install
```

The install exports `JellyFrame::jellyframe_render_core`, public headers, a
feature profile and a deterministic source manifest. Optional build families
are selected with `JELLYFRAME_ENABLE_CANVAS2D`,
`JELLYFRAME_ENABLE_MODERN_PAINT`, `JELLYFRAME_ENABLE_FLEX_GRID` and
`JELLYFRAME_ENABLE_ADVANCED_FORMS`.

The source manifest is an integrity/provenance identity for the source files;
it is not a signature or release authority. Consumers should lock both package
version and engine ABI according to their own compatibility policy.

The JellyFrame Runtime repository owns App packages, JerryScript integration,
desktop tooling and device-layer documentation.
