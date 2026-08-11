# Static Modules

> Last updated: 2026-07-10; Applies to: 0.5.0

This small package proves the package-time ES-module subset. `index.html` has
one external `type="module"` entry; `scripts/app.js` imports a package-local
helper. Packaging rewrites that entry to a generated classic script bundle, so
the device runtime does not need a module loader.

```powershell
python tools\jellyframe_cli.py preview --root samples\apps\packages\jelly_static_modules --output build\static_modules.bmp --build-dir build\desktop-release\Release
```
