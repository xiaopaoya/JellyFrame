# Weather Template

> Last updated: 2026-08-26; Applies to: 0.6.0-dev; Render Core baseline: 0.6.1

Brand-neutral weather app template for local-first data UI, package BMP image
resources, event delegation and future host network integration.

The template asks the host data service for `/data/weather.json` through the
JellyFrame `XMLHttpRequest` V0 subset when scripting is available, but keeps
local fallback data because remote page loading is intentionally not part of the
embedded core. It follows the shared template structure in
`../CONVENTIONS.md`.
