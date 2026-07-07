# Sample Launcher

> Last updated: 2026-07-07; Applies to: 0.5.0-dev

This is a JellyFrame app-authored launcher used by the Win32 host for bring-up,
CI and manual app-manager testing. It is a sample privileged system app, not a
fixed first-party launcher requirement.

The Win32 host currently injects the installed app list into
`<!-- JELLYFRAME_APP_LIST -->` and the status line into
`<!-- JELLYFRAME_STATUS -->`. A future system API can replace this template
bridge without changing the render pipeline.

The desktop registry mock now exposes V0 app-manager state: installed apps have
`status`, `enabled`, update time and optional rollback metadata. The Win32 shell
can install, remove, clear data and roll back apps through command-line
operations such as `--install-bundle`, `--remove-app`, `--delete-app-data`,
`--rollback-app`, `--enable-app` and `--disable-app`. This sample renders those
records, failure reasons, enabled state and rollback actions; real products
still own download, signature verification, permission prompts and persistent
launcher UX.
