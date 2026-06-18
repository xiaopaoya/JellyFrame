# Text Backend


JellyFrame keeps font loading and platform text APIs outside `jellyframe_core`.
The core only needs two services:

- text measurement during layout;
- text painting during software rasterization.

Both services are optional. If the host does not provide them, the core falls
back to a small UTF-8-aware estimate and an ASCII bitmap painter. That fallback
is meant for bring-up and diagnostics, not production CJK typography.

## Core API

`src/core/text_backend.h` defines the layout-side API:

- `TextMetrics { width, line_height }`
- `TextMeasureCallback`
- `TextMeasureProvider`
- `measure_text(...)`
- `fallback_text_metrics(...)`

`LayoutEngine` accepts an optional provider:

```cpp
LayoutEngine layout_engine(style_resolver, TextMeasureProvider{measure, context});
```

The callback receives the UTF-8 text, CSS font size and CSS font weight. It
returns an unwrapped run width and a single-line height. The layout engine uses
that width to estimate wrapping within the available content width.

`src/core/software_renderer.h` still owns the paint-side callback:

- `TextPainter`
- `TextPaintCallback`

Text display commands now carry minimal paint semantics:

- horizontal alignment: start, center or end;
- single-line versus wrapped text.

Hosts that care about visual correctness should provide both measurement and
painting from the same font engine. If they disagree, text can be clipped or
wrapped differently from what is drawn.

`src/core/text_adapter.h` provides `HostTextAdapter`, a tiny bridge for LVGL or
vendor engines that already expose both services. The adapter owns no resources;
the host-owned context must outlive layout and rendering:

```cpp
HostTextAdapter adapter{measure_text, paint_text, host_font_context};
LayoutEngine layout(style_resolver, text_measure_provider_from_adapter(adapter));
SoftwareCompositor compositor(text_painter_from_adapter(adapter));
```

This helper exists to keep board ports consistent. It does not add font
discovery, shaping or caching to the core.

## Fallback Behavior

The built-in measurement fallback:

- walks UTF-8 by codepoint, not byte;
- counts ASCII as roughly `2/3 * font-size`;
- counts non-ASCII as roughly one em;
- adds a small right-side safety pad for non-empty text;
- increases width slightly for bold text.

The built-in paint fallback:

- draws a tiny 5x7 bitmap glyph set for ASCII letters, digits and common
  punctuation;
- draws one placeholder glyph for each non-ASCII codepoint;
- approximates bold text with a second stroke pass.

This keeps the core deterministic and tiny. It deliberately does not implement
font discovery, glyph caching, shaping, bidi, kerning or full Unicode fallback.

## Win32 Validation Backend

`examples/win32_browser.cpp` now injects GDI for both layout measurement and
text painting. This makes desktop captures much closer to what users see in the
interactive validation shell, especially for Chinese and other UTF-8 text.

This backend is not part of the embedded core. It is a desktop validation shell
feature.

## Embedded Integration Guidance

Good embedded backends:

- keep font resources owned by the host or board support package;
- expose a cheap measurement function from the same font data used for drawing;
- avoid allocation inside per-frame measurement and paint callbacks;
- cache shaped or measured strings in the host only if the target has enough
  memory and the invalidation model is clear;
- choose a fixed UI font set when possible.

Recommended options:

- static bitmap font atlas for ASCII/numeric watch UI;
- LVGL text measurement and draw bridge;
- vendor font engine for CJK products;
- shaping-capable backend only for devices that need complex scripts.

`src/core/bitmap_font.h` provides the first static bitmap font backend:

- `BitmapFontGlyph`: one monochrome glyph, addressed by Unicode codepoint;
- `BitmapFont`: glyph table plus line-height and fallback advance. The glyph
  table must be sorted by ascending Unicode codepoint because lookup uses
  binary search for large CJK packs;
- `BitmapFontContext`: selected font and integer scale;
- `bitmap_font_measure_callback`;
- `bitmap_font_paint_callback`.

A generated embedded font pack should look like normal C++ data:

```cpp
static constexpr std::uint8_t rows_digit_0[] = { /* row bitmasks */ };
static constexpr BitmapFontGlyph glyphs[] = {
    BitmapFontGlyph{0x30, 5, 7, 6, 1, rows_digit_0},
};
static constexpr BitmapFont font{glyphs, 1, 8, 6};
static BitmapFontContext font_context{&font, 1};
```

The same `font_context` should be passed to `TextMeasureProvider` and
`TextPainter`.

`jellyframe_font_pack_gen` can generate this structure from a BDF bitmap font:

```text
jellyframe_font_pack_gen --bdf font.bdf --chars used_chars.txt --output font_pack.h --name app_font
```

The generator supports glyphs wider than 8 pixels through
`BitmapFontGlyph::bytes_per_row`, which is required for practical Chinese
bitmap fonts.

## Font Subsetting

`jellyframe_capability_check` can help prepare embedded font packs:

```text
jellyframe_capability_check --emit-used-chars used_chars.txt app.html app.css app.js
jellyframe_capability_check --font-coverage font_chars.txt app.html app.css app.js
jellyframe_capability_check --font-budget 16x16 app.html app.css app.js
```

`used_chars.txt` contains the non-ASCII UTF-8 characters seen in source files,
including common numeric and named character references. `font_chars.txt` is a
plain UTF-8 text file listing characters provided by the embedded font pack.
Missing non-ASCII codepoints are reported before deployment.
`--font-budget WxH` prints a rough bitmap-pack byte estimate for the non-ASCII
subset, useful before deciding whether to include a larger Chinese character
set. The tool also prints a font profile recommendation:

- `tiny`: ASCII, digits and basic UI symbols are enough for bring-up or English-only UI.
- `tiny-plus-symbols`: keep the tiny font and add only the scanned non-ASCII symbols.
- `app-subset-cn`: the app uses a small Chinese subset; generate an app-specific pack.
- `cn-standard`: for domestic Chinese devices, use ASCII + common symbols + GB2312 level-1 Chinese as the reusable profile.
- `global-product`: mixed or non-Chinese scripts were detected; choose per-market subsets such as Latin Extended,
  Greek, Cyrillic, Kana or Hangul instead of one universal font.

Do not treat `cn-standard` as JellyFrame's global default. It is the recommended Chinese-market preset. Products with
tight flash budgets should prefer app-specific subsets, while global products should ship market-specific font packs.

The intended production path is:

1. Author normal HTML/CSS/JS text.
2. Run the capability checker to collect required non-ASCII characters.
3. Use an offline font tool to rasterize those glyphs from a licensed vector
   font into a static embedded bitmap font pack.
4. Link that pack into the host text backend.

## Current Limits

- Text layout is still simplified block/inline wrapping, not a full browser
  inline formatting context.
- No core font loading or font-family cascade is implemented.
- No HarfBuzz-style shaping, bidi, ligatures, kerning or hyphenation.
- The callback reports whole-run metrics; per-word and per-grapheme wrapping is
  still future work.

For wearable apps, this is enough for labels, buttons, forms, calculators,
timers, weather panels and simple article-like text. Production multilingual
apps should provide a platform text backend before judging layout quality.
