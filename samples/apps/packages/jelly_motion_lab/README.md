# jelly_motion_lab

> Last updated: 2026-09-09; Applies to: 0.6.0-dev; Render Core baseline: 0.6.2

A cyan gradient orb demonstrates requestAnimationFrame, scale, opacity and translation. Breathe and Slide select the mode; Pause cancels the pending animation callback. Existing low-power and soak capture workloads remain unchanged.

```powershell
.\build\desktop-scripting-release\Release\jellyframe_desktop_shell.exe --app samples/apps/packages/jelly_motion_lab --frame-script samples/apps/packages/jelly_motion_lab/capture_review.jfcapture
```

Longer 30fps soak capture for regression review:

```powershell
.\build\desktop-release\Release\jellyframe_desktop_shell.exe --app samples\apps\packages\jelly_motion_lab --frame-script samples\apps\packages\jelly_motion_lab\capture_soak_30fps.jfcapture
```

Low-power budget smoke test:

```powershell
.\build\desktop-release\Release\jellyframe_desktop_shell.exe --app samples\apps\packages\jelly_motion_lab --frame-script samples\apps\packages\jelly_motion_lab\capture_low_power_static.jfcapture
```

The frame scripts can also set `animation-fps` and `animation-callbacks` to
validate host budget behavior without changing the app source.
