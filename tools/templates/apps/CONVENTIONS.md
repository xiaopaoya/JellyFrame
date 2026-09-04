# App Template Conventions

> Last updated: 2026-09-04; Applies to: 0.6.0-dev; Render Core baseline: 0.6.2

Templates are intentionally small authoring starting points, not capability
fixtures. Keep their structure consistent so a new package is immediately
recognizable:

- `jellyframe.app.json` is the single manifest; retain its schema, active
  Runtime/Core minima, entry path and one explicit target before adding product
  permissions or capabilities.
- `index.html` starts with `<!doctype html>`, declares `lang="en"`, links one
  local stylesheet, contains one semantic `<main>` root and loads one local
  classic script at the end of `<body>`.
- `styles/app.css` owns presentation only. Use the documented subset and keep
  the fixed design viewport explicit when the template is round-300.
- `scripts/app.js` owns local interaction and state. Do not add host-specific
  globals, private shell APIs or a second module loader to a starter template.

Use a showcase package or a focused acceptance package to learn one advanced
feature. Do not turn a starter into a historical record of every subsystem.
