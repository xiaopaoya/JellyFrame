# Jelly Watch Face

Analog watch-face sample for the `transform: rotate(...)` and
`transform-origin` subset. The hands use classic JavaScript to update
`element.style.transform` once per second.

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\jelly_watch_face --report out\jelly_watch_face_check.json --targets round-300,rect-320x240,rect-172x320 --build-dir build\Release
```
