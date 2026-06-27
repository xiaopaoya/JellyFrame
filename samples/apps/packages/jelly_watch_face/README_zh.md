# Jelly Watch Face

用于验收 `transform: rotate(...)`、`transform-origin`、`border-radius: 50%`
和 `conic-gradient()` 进度环子集的模拟表盘示例。
三根指针通过 classic JavaScript 每秒写入一次 `element.style.transform`。

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\jelly_watch_face --report out\jelly_watch_face_check.json --targets round-300,rect-320x240,rect-172x320 --build-dir build\Release
```

使用带 JerryScript 的 Win32 壳可以生成 30fps 验收拼图，观察指针旋转、
`conic-gradient()` 圆环和圆角抗锯齿是否稳定：

```powershell
.\build-script\Release\jellyframe_win32_browser.exe --app samples\apps\packages\jelly_watch_face --frame-script samples\apps\packages\jelly_watch_face\capture_watch_face_30fps.jfcapture
```
