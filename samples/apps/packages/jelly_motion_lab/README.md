# Jelly Motion Lab

Packaged animation fixture for validating watch-style JellyFrame motion. It
uses standard CSS `@keyframes`, `transform`, `opacity`, transitions and
`requestAnimationFrame`; no custom motion API is required.

Useful Win32 capture:

```powershell
.\build\Release\jellyframe_win32_browser.exe --app samples\apps\packages\jelly_motion_lab --capture-frames out\motion_lab_frames --frame-count 30 --frame-step-ms 33 --viewport-width 300 --viewport-height 300
```

Preferred deterministic scripted capture:

```powershell
.\build\Release\jellyframe_win32_browser.exe --app samples\apps\packages\jelly_motion_lab --frame-script samples\apps\packages\jelly_motion_lab\capture_30fps.jfcapture
```
