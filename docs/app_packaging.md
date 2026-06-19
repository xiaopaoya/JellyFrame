# App Packaging

JellyFrame app packaging turns web-like source files into deterministic,
firmware-friendly app resources. The goal is not to copy a phone/watch app store
package. JellyFrame keeps a small browser-like authoring model while producing
resources that can be loaded without a filesystem, network stack or runtime
archive parser.

There are two deployment shapes:

- Static C++ resource tables are for firmware-built apps: launchers, watch faces,
  app lists, factory apps, bring-up fixtures and safe fallback UI.
- Flash-resident installable bundles are the intended path for third-party apps.
  They are not implemented yet, but the package format and resource boundary
  should evolve toward this path. If third-party apps required reflashing
  firmware, JellyFrame would lose its main advantage over a conventional LVGL/C
  firmware UI.

## Survey Summary

The reviewed platforms converge on a few practical patterns:

- Xiaomi Vela JS apps use `src/manifest.json` for app identity, version,
  permissions, system configuration and page routing. Projects keep source
  under `src/`, with `app.ux`, page files, shared resources and i18n files.
  Build output goes to generated package artifacts.
- Zepp OS Mini Programs use a root `app.json` with app metadata, runtime
  requirements, permissions, targets and i18n. Target-specific assets live under
  per-device directories named from the `targets` configuration.
- HarmonyOS app packages are richer than JellyFrame needs, but the Stage model
  still reinforces the split between app-level configuration, module
  configuration and resources.
- Android App Bundle is a publishing format, not the exact runtime installable
  artifact. Its useful lesson for JellyFrame is device-specific output: keep a
  full source bundle, then generate smaller target bundles.
- Apple bundles use a standardized hierarchy and `Info.plist`-style metadata so
  code can find resources predictably.
- MSIX uses an explicit package manifest and file mapping/integrity metadata.
  The integrity idea is useful; the container and installation model are too
  heavy for the embedded core.

## Design Goals

- Source packages stay friendly to web authors: HTML, CSS, classic JS, assets,
  fonts and i18n files.
- Build output is deterministic: sorted resources, normalized paths and stable
  generated C++ tables or future binary installable bundles.
- Runtime loading is O(log n) or bounded linear for very small bundles, with no
  heap-heavy archive parsing.
- All budgets are declared before deployment: resource bytes, DOM nodes, CSS
  rules, display commands, timers, listeners and framebuffer policy.
- Core package loading stays filesystem-free and network-free. Hosts provide
  package bytes through the existing `HostResourceLoader` boundary.
- Apps may request network capability for runtime data APIs. That capability is
  declared separately from package resource loading, so remote pages and remote
  CSS/script/image resources remain disabled without closing the door on future
  host-provided fetch APIs.
- Missing optional resources degrade cleanly; missing required entry resources
  fail package validation before flashing.

## Source Package Layout

Recommended source layout:

```text
my_app/
  jellyframe.app.json
  index.html
  styles/
    app.css
  scripts/
    app.js
  assets/
    icon.png
    images/
  fonts/
    ui.bdf
  i18n/
    en-US.json
    zh-CN.json
```

Only `jellyframe.app.json` and the declared entry HTML are required. Everything
else is optional and should be referenced through local absolute or relative
paths.

Development-only files such as `README.md`, `README_zh.md`, hidden directories,
`__pycache__`, `.DS_Store` and `Thumbs.db` are ignored by the packer. They can
document source packages without inflating runtime resources or font coverage
reports.

## Manifest V0

The first manifest should stay small and JSON-based because it is consumed by
desktop tools, not by the MCU runtime:

```json
{
  "$schema": "../../../tools/schemas/jellyframe.app.schema.json",
  "format": "jellyframe.app",
  "formatVersion": 0,
  "id": "com.example.weather",
  "name": "Weather",
  "version": {
    "name": "0.1.0",
    "code": 1
  },
  "entry": "/index.html",
  "runtime": {
    "minJellyFrame": "0.4.0",
    "script": "classic"
  },
  "viewport": {
    "designWidth": 300,
    "designHeight": 300,
    "shape": "round"
  },
  "budgets": {
    "maxResourceBytes": 131072,
    "maxDomNodes": 512,
    "maxCssRules": 256,
    "maxDisplayCommands": 512
  },
  "fonts": [
    {
      "id": "ui",
      "source": "fonts/ui.bdf",
      "profile": "app-subset-cn",
      "sizes": [16, 20],
      "weights": [400, 700]
    }
  ],
  "targets": {
    "esp32s3-round-300": {
      "viewport": {
        "width": 300,
        "height": 300,
        "shape": "round"
      },
      "fontProfile": "app-subset-cn",
      "output": "cpp"
    }
  },
  "permissions": ["network"],
  "capabilities": ["network.fetch"]
}
```

The schema is stored at `tools/schemas/jellyframe.app.schema.json`. The developer CLI
can print it or its path:

```powershell
python tools/jellyframe_cli.py schema --print-path
```

Built-in target presets live under `tools/presets/targets`. List them with:

```powershell
python tools/jellyframe_cli.py targets
```

Built-in source-package templates live under `tools/templates/apps`. List and create
them with:

```powershell
python tools/jellyframe_cli.py templates
python tools/jellyframe_cli.py new `
  --template calculator `
  --output build/my_calculator `
  --id org.example.calculator `
  --name Calculator `
  --target round-300
```

When `--target id` is used, the packer first loads `tools/presets/targets/id.json`
when present, then overlays any same-named manifest `targets[id]` settings.
Manifest-only custom targets are also allowed; unknown targets fail explicitly.

Fields that affect runtime compatibility must be required by the packer:

- `id`, `version.code`, `entry`, `runtime.minJellyFrame`;
- `viewport` or target-specific viewport;
- `budgets.maxResourceBytes`;
- target name and output kind.

`permissions: ["network"]` and `capabilities: ["network.fetch"]` declare that
the app expects a future host-provided network request API for runtime data.
They do not enable remote package resources. The packer records these fields in
the report so product firmware can reject apps whose requested capabilities do
not match the board policy.

## Resource Path Rules

JellyFrame should use a strict local path subset:

- Package resource URLs are local only: no scheme, no `//host`, no query-driven
  network fetch.
- Absolute app paths start with `/`.
- Relative paths resolve against the referring resource directory.
- `.` is ignored; `..` is rejected when it escapes the app root.
- Build tools normalize separators to `/`, reject duplicate normalized paths
  and sort entries by normalized path.
- CSS `url(...)`, `<link href>`, `<script src>`, images and fonts all use the
  same resolver.

These rules match the current ESP32-S3 bring-up closely and keep the MCU loader
small.

## Build Outputs

The tools can produce two output forms from the same manifest.

Desktop/debug output:

```text
dist/my_app.jfdir/
  jellyframe.package.json
  resources...
```

Built-in firmware output:

```text
dist/my_app_resources.cpp
dist/my_app_resources.h
dist/my_app_font.h
dist/my_app_report.json
```

Planned third-party installation output:

```text
dist/my_app.jfapp
dist/my_app_report.json
```

The static C++ table and the future binary bundle should expose the same logical
resource map to the runtime. The renderer and script runtime should not care
whether bytes came from a compiled table, a flash partition, a debug folder or a
board-specific app store.

The generated resource table should include:

- normalized path;
- resource kind;
- byte pointer or blob offset;
- byte size;
- optional checksum for diagnostics;
- optional path hash for fast lookup.

For small bundles, sorted linear lookup is acceptable and simpler. For larger
bundles, use sorted FNV-1a path hashes plus string confirmation to avoid hash
collision surprises.

## Packaging Pipeline

Recommended desktop pipeline:

1. Validate `jellyframe.app.json` and normalize paths.
2. Walk entry HTML, linked stylesheets and classic scripts.
3. Run real pipeline diagnostics from the components that actually parsed,
   styled, laid out, rendered or loaded the app. The old text-search
   compatibility scanner is retired; diagnostics should come from parser, CSS,
   style, layout, layer, renderer, scripting and package loading components.
4. Generate or verify bitmap font packs with `jellyframe_font_pack_gen`.
5. Enforce budgets: per-resource bytes, total bundle bytes, CSS rule estimates
   and script byte limits.
6. Emit generated resources for the selected target.
7. Emit a report containing warnings, pipeline diagnostics, optional font
   coverage, estimated memory and final resource table.

## Explicit Cuts

The embedded package model intentionally does not include:

- runtime ZIP extraction or general archive parsing on MCU;
- dependency package managers;
- dynamic native libraries;
- arbitrary remote package resources;
- service workers, caches or browser storage;
- multi-process install/update semantics;
- page module transpilation comparable to Vela `.ux` or HarmonyOS ArkUI.

Those features belong outside the embedded core or to a future desktop
packaging layer.

## Current Tooling

The recommended developer entry point is `tools/jellyframe_cli.py`. Validate
only:

```powershell
python tools/jellyframe_cli.py validate `
  --root samples/apps/packages/watch_weather `
  --report build/watch_weather_report.json
```

Generate a C++ resource table and report:

```powershell
python tools/jellyframe_cli.py package `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --output-cpp build/watch_weather_resources.cpp `
  --report build/watch_weather_report.json
```

Generate a copied debug directory for editor tooling or inspection:

```powershell
python tools/jellyframe_cli.py package `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --output-cpp build/watch_weather_resources.cpp `
  --report build/watch_weather_report.json `
  --debug-dir build/watch_weather.jfdir
```

The pseudo browser can open a source package directory directly:

```powershell
python tools/jellyframe_cli.py preview `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --output build/watch_weather.ppm
```

`package` and `check` run package validation first, then run a temporary
pseudo-browser pipeline pass so parser/style/layout/layer diagnostics come from
the components that actually handled the app. `preview` is already a full
pipeline run, so it does not duplicate that preflight. Pass `--font-budget`,
`--font-coverage` or `--emit-used-chars` when you also want the font resource
audit before packaging or previewing.

The CLI merges pseudo-browser output into the selected JSON report as
`pipelineDiagnostics`. Any diagnostic with severity `error` fails `check`,
`preview` and `package`. Warnings are emitted into the report but do not fail by
default; pass `--strict` when CI or release packaging should reject warnings as
well.

For human app authoring on Windows, prefer the interactive Win32 browser shell:

```powershell
.\build\Release\jellyframe_win32_browser.exe --app samples\apps\packages\watch_weather
```

The pseudo browser remains the deterministic CI/capture shell. It is still
important because it can render without a window, but it should not be treated
as the primary human debugging surface for interactive apps.

Run package validation plus optional font resource checks on packaged files:

```powershell
python tools/jellyframe_cli.py check `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --report build/watch_weather_report.json `
  --font-budget 16x16
```

Collect package characters and optionally generate an embedded bitmap font
header:

```powershell
python tools/jellyframe_cli.py font `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --report build/watch_weather_report.json `
  --used-chars build/watch_weather_used_chars.txt `
  --font-budget 16x16
```

When a BDF source font is available, add `--bdf` and `--output-header`:

```powershell
python tools/jellyframe_cli.py font `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --report build/watch_weather_report.json `
  --used-chars build/watch_weather_used_chars.txt `
  --font-budget 16x16 `
  --bdf path/to/ui.bdf `
  --output-header build/watch_weather_font.h `
  --name watch_weather_font
```

The pseudo browser reports whether the manifest requested network capability,
but network requests are not executed yet.

The JSON report is intended for CI and editor integrations. It contains app
metadata, selected target config, effective budgets, resource sizes,
CRC32/SHA-256 checksums, local/remote reference diagnostics, package-resource
warnings and `pipelineDiagnostics`. Pipeline diagnostics include the
pseudo-browser format/version marker, output viewport, package resource load
counts, memory-oriented pipeline statistics, script status, a severity summary
and the concrete diagnostics emitted by parser, style, layout, layer, renderer,
scripting or package loading code. Known unsupported/degraded features should
include a precise reason; unknown recovery should at least include the
triggering field or snippet.

`tools/package_app.py` remains the lower-level packer used by the CLI and
embedded build integrations.

The built-in weather, clock, timer and calculator templates are ordinary source
packages. They cover event delegation, `dataset`, timers, grid layout and form
controls without adding another app framework above HTML/CSS/JS.

An optional VS Code helper lives under `tools/vscode-jellyframe`. It contributes
manifest schema association and command-palette actions that call
`tools/jellyframe_cli.py` for validate, check, preview, package and template
creation, plus a command that launches the Win32 interactive browser for the
selected package. It reads the CLI report to show a report panel and inline
diagnostics for package warnings and `pipelineDiagnostics`. The CLI remains the
source of truth for CI and non-VS-Code workflows.

References:

- Xiaomi Vela JS App project configuration and project structure:
  <https://iot.mi.com/vela/quickapp/en/guide/framework/manifest.html>,
  <https://iot.mi.com/vela/quickapp/en/guide/start/project-overview.html>
- Zepp OS Mini Program configuration and folder structure:
  <https://docs.zepp.com/docs/reference/app-json/>,
  <https://docs.zepp.com/docs/v2/guides/architecture/folder-structure/>
- HarmonyOS application package structure:
  <https://developer.huawei.com/consumer/en/doc/harmonyos-guides/application-package-structure-stage>
- Android App Bundle format:
  <https://developer.android.com/guide/app-bundle/app-bundle-format>
- Apple bundle resources:
  <https://developer.apple.com/documentation/BundleResources/placing-content-in-a-bundle>
- MSIX packaging:
  <https://learn.microsoft.com/en-us/windows/msix/package/packaging-uwp-apps>
