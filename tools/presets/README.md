# Presets

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Target and packaging presets used by JellyFrame desktop tools.

Presets describe device-oriented budgets and capabilities in data files so app
authors can validate packages without hard-coding a specific board in the core.

Start with [`targets/README.md`](targets/README.md). The JSON files in
`targets/` are data, not board drivers and not a replacement for port-owned
hardware acceptance.
