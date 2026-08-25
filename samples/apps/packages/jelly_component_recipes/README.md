# Component Recipes

> Last updated: 2026-08-26; Applies to: 0.6.0-dev; Render Core baseline: 0.6.1

Deterministic scroll and dirty-region acceptance input for common wearable UI
structure. The copyable author recipes are maintained in
`docs/app_author_recipes.md`; this package stays focused on replay coverage.

- Top bar, round-first status card, card stack, compact buttons, control rows
  and fixed bottom navigation.
- One explicit `overflow: auto` content area, with navigation outside it.
- No host services, images, Canvas or JavaScript.
- The manifest gates `round-300`, `rect-320x240` and `rect-172x320`.

Check it with:

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\jelly_component_recipes --target round-300 --targets round-300,rect-320x240,rect-172x320 --build-dir build\desktop-release\Release
```

Run the deterministic scroll capture when validating present/dirty-rect behavior:

```powershell
.\build\desktop-release\Release\jellyframe_desktop_shell.exe --app samples\apps\packages\jelly_component_recipes --frame-script samples\apps\packages\jelly_component_recipes\capture_scroll_recipes.jfcapture
```

The summary should report internal scrolls with dirty repaint and no non-initial
full repaint.
