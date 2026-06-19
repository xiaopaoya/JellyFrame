# Sample Launcher

This is a JellyFrame app-authored launcher used by the Win32 host for bring-up,
CI and manual app-manager testing. It is a sample privileged system app, not a
fixed first-party launcher requirement.

The Win32 host currently injects the installed app list into
`<!-- JELLYFRAME_APP_LIST -->` and the status line into
`<!-- JELLYFRAME_STATUS -->`. A future system API can replace this template
bridge without changing the render pipeline.
