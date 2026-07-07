# Schemas

> Last updated: 2026-07-07; Applies to: 0.6.0-dev

JSON Schema files for JellyFrame app packages and tool inputs.

These schemas are intended for editor validation, CI checks and the VS Code
helper extension. They are not used by embedded runtime code.

- `jellyframe.app.schema.json`: source-package manifest schema.
- `jellyframe.installed_apps.registry.schema.json`: desktop/system-shell
  installed-app registry mock schema. `status` is limited to `installed`,
  `disabled` and `failed`; `rollback-ready` is a derived display state from the
  optional `rollback` record, not a persistent status value.
