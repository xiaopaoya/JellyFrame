# Schemas

> Last updated: 2026-07-10; Applies to: 0.5.0-dev

JSON Schema files for JellyFrame app packages and tool inputs.

These schemas are intended for editor validation, CI checks and the VS Code
helper extension. They are not used by embedded runtime code.

- `jellyframe.app.schema.json`: source-package manifest schema.
- `jellyframe.install_candidate.schema.json`: host-prepared local candidate for
  a downloaded `.jfapp`. It records bundle hash, host signature-verification
  status and user approval before the registry commits the bundle.
- `jellyframe.app_manager.state.schema.json`: launcher-friendly derived state
  report generated from the installed-app registry. Launchers can consume this
  instead of re-deriving `launchable`, `rollbackReady` and `failure` fields.
- `jellyframe.installed_apps.registry.schema.json`: desktop/system-shell
  installed-app registry mock schema. `status` is limited to `installed`,
  `disabled` and `failed`; `rollback-ready` is a derived display state from the
  optional `rollback` record, not a persistent status value.
