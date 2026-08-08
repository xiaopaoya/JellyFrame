# Sample Launcher

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

This is a JellyFrame app-authored launcher used by the Win32 host for bring-up,
CI and manual app-manager testing. It is a sample privileged system app, not a
fixed first-party launcher requirement.

The Win32 host currently injects the installed app list into
`<!-- JELLYFRAME_APP_LIST -->` and the status line into
`<!-- JELLYFRAME_STATUS -->`. A future system API can replace this template
bridge without changing the render pipeline.

The desktop registry mock now exposes V0 app-manager state: installed apps have
`status`, `enabled`, update time and optional rollback metadata. Its compact
device-library layout separates store state, launchability and recovery status
from the primary Open action and lower-emphasis maintenance actions. It exposes
host-owned launch, enable/disable, rollback, clear data, remove-and-delete-data,
and remove-and-keep-data actions. Destructive data changes require a separate
host-owned confirmation step before the registry or app data is modified. The Win32 shell
accepts a raw local bundle with `--install-bundle` for bring-up, and a
host-prepared `--install-candidate candidate.json` for the verified path. A
candidate must identify the local bundle by SHA-256, declare a `trusted`
signature result, and record explicit user approval before the host commits it.
The host still owns download, TLS, signature verification, permission prompts
and persistent launcher UX; ordinary apps receive no installation API.
Before a new install, it also clears abandoned staging files and bundles that
are no longer referenced by a current or rollback registry entry. This recovery
is host-owned and never removes app-private data.
