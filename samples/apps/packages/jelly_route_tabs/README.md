# Route Tabs

> Last updated: 2026-07-12; Applies to: 0.5.0-dev

A single-package tabbed settings/focus flow using the bounded app-local
`location.hash` subset. It changes only route state inside the running app:
there is no URL loading, browser history or navigation stack.

```powershell
python tools\jellyframe_cli.py preview --root samples\apps\packages\jelly_route_tabs --output build\route_tabs.bmp --build-dir build\Release
```

For a deterministic Win32 interaction capture, run:

```powershell
.\build\Release\jellyframe_win32_browser.exe --app samples\apps\packages\jelly_route_tabs --frame-script samples\apps\packages\jelly_route_tabs\capture_route_tabs.jfcapture
```
