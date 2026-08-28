# App Author Environment

> Last updated: 2026-08-28; Applies to: 0.6.0-dev

JellyFrame has two distinct audiences:

- **App authors** need the VS Code extension, an installed JellyFrame SDK, and a Device OS
  provider. They should not put an App inside the JellyFrame source checkout or install ESP-IDF,
  JerryScript, or the full repository build.
- **Framework and port maintainers** need the complete source tree, Render Core, Runtime, ports,
  hardware toolchains, and process documentation.

An App is an independent workspace containing `jellyframe.app.json`, `index.html`, and its local
CSS/JavaScript resources. Run **JellyFrame: Configure Author Environment** once in VS Code and
select the installed SDK. The extension stores the machine-level SDK path, so later independent
App workspaces need no `repoRoot` setting. A project may pin an SDK with `.jellyframe/project.json`:

```json
{
  "format": "jellyframe.app.project",
  "formatVersion": 1,
  "sdkRoot": "C:/JellyFrame/sdk"
}
```

The value may be absolute or relative to the App workspace. `JELLYFRAME_SDK_ROOT` is also
recognized. An SDK must contain `tools/jellyframe_cli.py`. A packaged author SDK normally includes
the desktop runtime; a source checkout may build one locally. A minimal SDK without either is
reported as incomplete instead of attempting a doomed CMake build.

Reports, captures, frame scripts, and temporary resources default to the App's
`.jellyframe/build/`, keeping the SDK and source checkout clean. `jellyframe.buildDir` remains an
explicit override for shared or CI output.

The VSIX includes the App manifest schema, so an independent workspace does not depend on a
repository-relative schema path. Templates use an accessible GitHub raw schema URL; installed
VS Code uses the bundled schema, including offline. Do not copy the obsolete `jellyframe.dev` URL
or a `../../../../tools/schemas` path into a new project.

The distributed App-author SDK should contain the CLI, schema, target presets, and a matching
prebuilt desktop shell. A Device OS provider is a separate versioned package. The extension does
not silently download source, modify projects, or guess a device port; missing environment pieces
produce a concrete configuration action and path requirement.

Maintainers create the SDK ZIP with `project_tools/package_app_author_sdk.py` from a validated
standard desktop Release, passing a scripting Release when classic-script App debugging is part of
the delivery. Its manifest records file SHA-256 values and the included desktop profiles. Release
the archive only after an unpacked CLI/template smoke test; never substitute an arbitrary source
checkout or unverified local build directory for an App-author SDK.
