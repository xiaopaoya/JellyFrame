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
usable on several wearable form factors. Single-target commands keep the older
report shape and do not emit `responsiveProfiles[]`.

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
    "audioPlayback": false
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
app-private KV storage as supported, and audio playback as unsupported. Product
ports with a real codec/speaker path should override `audioPlayback` in their
own preset rather than relying on the generic display-shape preset.
