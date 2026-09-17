# Font Size Install Warning Probe

> Last updated: 2026-09-17; Applies to: 0.6.0-dev; Render Core baseline: 0.6.2

This acceptance package exists only to trigger the VS Code extension's pre-install font-size warning deterministically.

- Its packaged `.jffont` has an `8px` native line height.
- The current app-font runtime supports integer scales from `1x` through `8x`, so the exact sizes are
  `8/16/24/32/40/48/56/64px`.
- The manifest declares and CSS requests `12px`. Runtime therefore selects `16px`, and packaging must emit exactly one
  `font-size-unavailable` warning.
- The package declares unique `172x320`, `300x300`, and `320x240` targets so the extension can match common test devices.

## Extension test

1. Open this directory in VS Code, or select the App in the JellyFrame sidebar.
2. Connect and select a device.
3. Run **Package and Deploy Current App**.
4. After accepting the ordinary deployment confirmation, a second modal warning must report the affected `12px` size.
5. **View report** must stop installation and open the report. Run again and choose **Install anyway** to continue to device installation.

The report can be verified without a device:

```powershell
python tools\package_app.py `
  --root samples\apps\packages\jelly_font_size_warning `
  --target rect-172x320 `
  --report build\font-size-warning.report.json `
  --output-bundle build\font-size-warning.jfapp
```

`warnings[0].code` must be `font-size-unavailable`, with `sizes` equal to `[12]`.
