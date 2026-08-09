# Desktop Debug Tools

> Last updated: 2026-08-10; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

This is the app-author-facing entry point for desktop inspection. It discovers
`jellyframe_desktop_shell` and accepts the historical
`jellyframe_win32_browser` executable while older builds are still around.

```powershell
python tools\debug\jellyframe_debug.py --list-builds
python tools\debug\jellyframe_debug.py --app samples\apps\packages\watch_weather
python tools\debug\jellyframe_debug.py --app samples\apps\packages\watch_weather --capture build\watch_weather.bmp --wait
python tools\debug\jellyframe_debug.py --app tests\fixtures\apps\jelly_scroll_probe --frame-script tests\fixtures\apps\jelly_scroll_probe\capture_wheel_scroll.jfcapture --wait
python tools\debug\jellyframe_debug.py --build-dir build\Debug -- --help
```

The facade only launches the native shell. It does not claim MCU timing,
panel/DMA behavior or hardware font fidelity. Use port acceptance procedures
for those claims.
