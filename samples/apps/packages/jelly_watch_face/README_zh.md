# Jelly Watch Face

用于验收 `transform: rotate(...)` 和 `transform-origin` 子集的模拟表盘示例。
三根指针通过 classic JavaScript 每秒写入一次 `element.style.transform`。

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\jelly_watch_face --report out\jelly_watch_face_check.json --targets round-300,rect-320x240,rect-172x320 --build-dir build\Release
```
