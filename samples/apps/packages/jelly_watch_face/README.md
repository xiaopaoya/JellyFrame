# Jelly Watch Face

> Last updated: 2026-07-07; Applies to: 0.5.0

Analog watch-face sample for the `transform: rotate(...)`,
`transform-origin`, `border-radius: 50%` and `conic-gradient()` progress-ring
subsets. The hands use classic JavaScript to update `element.style.transform`
once per second. The script builds `new Date(Date.now())` so Win32 frame scripts
and MCU hosts can inject deterministic wall-clock time through the standard
`Date.now()` subset.

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\jelly_watch_face --report out\jelly_watch_face_check.json --targets round-300,rect-320x240,rect-172x320 --build-dir build\desktop-release\Release
```

With the JerryScript-enabled Win32 shell, generate a 30fps contact sheet to
inspect hand rotation, the `conic-gradient()` ring and rounded antialiasing:

```powershell
.\build\desktop-scripting-release\Release\jellyframe_desktop_shell.exe --app samples\apps\packages\jelly_watch_face --frame-script samples\apps\packages\jelly_watch_face\capture_watch_face_30fps.jfcapture
```
