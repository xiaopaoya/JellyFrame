# Jelly Watch Face

Analog watch-face sample for the `transform: rotate(...)`,
`transform-origin`, `border-radius: 50%` and `conic-gradient()` progress-ring
subsets. The hands use classic JavaScript to update `element.style.transform`
once per second.

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\jelly_watch_face --report out\jelly_watch_face_check.json --targets round-300,rect-320x240,rect-172x320 --build-dir build\Release
```

With the JerryScript-enabled Win32 shell, generate a 30fps contact sheet to
inspect hand rotation, the `conic-gradient()` ring and rounded antialiasing:

```powershell
.\build-script\Release\jellyframe_win32_browser.exe --app samples\apps\packages\jelly_watch_face --frame-script samples\apps\packages\jelly_watch_face\capture_watch_face_30fps.jfcapture
```
