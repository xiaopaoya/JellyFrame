# Render Core Samples

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Samples in this directory exercise the platform-neutral render pipeline only.
They should not depend on `.jfapp` manifests, app registries, network services,
storage services or JerryScript.

- `pages/modern`: standalone modern HTML/CSS pages for graceful-degradation and
  screenshot checks.
- `fonts/bitmap`: small bitmap-font inputs for render-core font tooling smoke
  tests.

Audience: Render Core contributors and desktop visual reviewers. App authors
should start with `tools/templates/apps/` or `samples/apps/packages/`, because
these pages deliberately omit app manifests and host/runtime behavior.
