# Service Status

Small runtime-service sample for the optional data and media service boundary.

It declares network, storage and MP3 playback capabilities, then uses
`backgroundServices` to express which work may continue while the app is
suspended or the screen is off. The manifest is only an intent signal: the host
profile still decides which background activity is allowed on a given device.

The page uses only local HTML/CSS/JS. It reads `navigator.onLine` and
`document.hidden`, requests `/data/service-status.json` through
`XMLHttpRequest`, and writes the latest service state into the `localStorage` shadow so the Win32
shell can validate system-state delivery, host completions and small
non-blocking storage together. It does not pretend that the public JS audio or
sensor API already exists.

Deterministic Win32 validation:

```powershell
.\build-script\Release\jellyframe_win32_browser.exe `
  --app samples\apps\packages\jelly_service_status `
  --frame-script samples\apps\packages\jelly_service_status\capture_system_events.jfcapture
```

The capture summary should include non-zero network host completions, system
events handled by script and `service_activity` counters. In the bundled script,
network activity stops while the screen is hidden, audio remains allowed, and
sensors are throttled during screen-off or low-power frames.
