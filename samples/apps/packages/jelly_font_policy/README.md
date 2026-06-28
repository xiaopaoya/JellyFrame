# Jelly Font Policy

Small package used to validate the app font policy path:

- CSS declares two package families: `Jelly Tiny CN` and `Jelly Tiny Symbols`.
- `jellyframe.app.json` declares both families and points to package-local
  `.jffont` supplements.
- `jellyframe_cli.py check` reports both families as manifest runtime fonts,
  validates glyph coverage before install and intentionally warns about the
  missing `あ` probe.
- Win32 can validate the runtime text path with `--use-app-fonts`:

```powershell
.\build-script\Release\jellyframe_win32_browser.exe --app samples\apps\packages\jelly_font_policy --use-app-fonts
```

The font is deliberately tiny and only exists for deterministic package/tool
tests. Product apps should generate their own `.jffont` subsets from licensed
bitmap fonts.
