# Pre-1.0 Evolution Policy

> Last updated: 2026-08-15; Applies to: 0.6.0-dev

JellyFrame has no production compatibility obligation before `1.0`. The
project's active users are development and evaluation users working from the
current source line. Architecture clarity, one authoritative implementation,
measurable performance and maintainability take priority over preserving an
old pre-release behavior.

## Rules

- A `0.y` development-line change may intentionally break a prior pre-1.0
  API, manifest field, package summary, diagnostic, tool command or internal
  module boundary when it leaves the current contract clearer or safer.
- Do not add legacy parsers, aliases, adapters, dual code paths or silent
  fallback behavior merely to accept an older JellyFrame development artifact.
  Remove or reject the stale form instead.
- A breaking change updates its canonical implementation, first-party samples,
  schemas, documentation and regression tests in the same change. There is no
  separate migration-support branch.
- Keep only compatibility mechanisms that define a current architectural
  boundary: the Render Core package version/ABI lock, feature-profile contract,
  and explicit host/port ownership contracts. These are not legacy shims.
- Preserve evidence boundaries. A refactor does not promote `partial` or
  `not-tested` device behavior to `supported`; hardware claims still require
  port-owned evidence.

## Version Discipline

- `0.y.0` marks a new active pre-1.0 contract line and may be breaking.
- `0.y.z` is normally a focused fix, but may still remove an incorrect or
  harmful pre-1.0 contract when retaining it would create duplicate ownership
  or long-term maintenance cost.
- `-dev` builds are mutable development artifacts, not a compatibility target.
- `1.0` is the first point at which supported public contracts require a
  compatibility policy and, where necessary, a deliberate migration plan.

The current official samples and templates target the active `0.6.0` Runtime
and Render Core lines. Older `minJellyFrame` or `minRenderCore` values are
historical metadata, not supported production targets.
