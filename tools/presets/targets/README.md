# Target Presets

Device capability presets consumed by packaging and validation tools.

Keep these files conservative. A preset should describe what an app can rely on,
not every optional feature a particular board might expose.

`jellyframe_cli.py check`, `preview`, `package` and `install` can run an
explicit multi-target responsive validation pass with `--targets` or
`--all-targets`. The pass renders the same package once per preset through the
render-core pseudo browser and stores compact results in `responsiveProfiles[]`
inside the JSON report.

A responsive profile is a desktop developer signal, not a runtime feature. It
reports viewport size, shape, content height, horizontal overflow, scroll need
and diagnostic counts so an app author can see whether one package remains
usable on several wearable form factors. Each profile also carries the
pseudo-browser `frameUpdate` summary for the first render, normally
`first-paint` / `full-frame`, so authors can separate expected first-frame cost
from later Win32 frame-loop repaint fallback data. The pseudo-browser report
also exposes
`paintBounds` next to raw layout bounds; horizontal overflow is based on visible
display-list bounds so clipped implementation boxes do not create false
failures. Single-target commands keep the older report shape and do not emit
`responsiveProfiles[]`.

Font budgets (`maxAppFonts`, `maxAppFontBytes`, `maxAppFontGlyphs`) are tooling
limits for installable `.jffont` supplements. They should reflect flash/storage
and install-policy expectations, not the system firmware font that every app
already shares.

Optional host services may be described with `hostServices`:

```json
{
  "hostServices": {
    "networkFetch": true,
    "storageKv": true,
    "audioPlayback": false,
    "sensorAccelerometer": false,
    "sensorGyroscope": false,
    "sensorHeartRate": false,
    "sensorAmbientLight": false,
    "locationPosition": false
  }
}
```

This feeds package-report `serviceIntent.targetSupport` as
`supported`/`unsupported`/`unknown`. It is a developer compatibility signal, not
an app permission grant. When an app requests a service that a selected preset
explicitly marks `false`, packaging also emits a `service-target-unsupported`
warning. Missing keys stay `unknown` rather than failing, because product ports
may define optional services outside the generic preset.

The built-in wearable presets currently mark bounded runtime network fetch and
app-private KV storage as supported, and audio playback as unsupported. Sensor
and location capabilities may remain `unknown` by default unless a product
preset can guarantee the semantic data service. Product ports with a real
codec/speaker path or sensor/location service should override the matching
fields in their own preset rather than relying on the generic display-shape
preset.

Current generic display shapes include `round-300`, `rect-320x240` and
`rect-172x320`; `esp32s3-round-300` adds an ESP32-S3-oriented RGB565 profile.
The `rect-172x320` preset is intended for narrow portrait wearable panels such
as Waveshare ESP32-S3-Touch-LCD-1.47 class boards.
