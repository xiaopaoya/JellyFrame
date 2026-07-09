# Component Recipes

> Last updated: 2026-07-10; Applies to: 0.5.0-dev

Small app-author recipes for common wearable UI structure.

- Top bar, round-first status card, card stack, compact buttons, control rows
  and fixed bottom navigation.
- One explicit `overflow: auto` content area, with navigation outside it.
- No host services, images, Canvas or JavaScript.
- The manifest gates `round-300`, `rect-320x240` and `rect-172x320`.

Check it with:

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\jelly_component_recipes --target round-300 --targets round-300,rect-320x240,rect-172x320 --build-dir build\Release
```

Run the deterministic scroll capture when validating present/dirty-rect behavior:

```powershell
.\build\Release\jellyframe_win32_browser.exe --app samples\apps\packages\jelly_component_recipes --frame-script samples\apps\packages\jelly_component_recipes\capture_scroll_recipes.jfcapture
```

The summary should report internal scrolls with dirty repaint and no non-initial
full repaint.
