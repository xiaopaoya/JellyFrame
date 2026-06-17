# 文本后端

日期：2026-06-15

JellyFrame 不把字体加载和平台文本 API 放进 `jellyframe_core`。核心只需要两类服务：

- layout 阶段的文本测量；
- software rasterization 阶段的文本绘制。

这两类服务都是可选的。宿主不提供时，核心会退回到一个很小的、按 UTF-8
码点估算的测量逻辑，以及 ASCII bitmap fallback 绘制器。这个 fallback
用于 bring-up 和诊断，不作为正式 CJK 排版方案。

## 核心 API

`src/core/text_backend.h` 定义 layout 侧 API：

- `TextMetrics { width, line_height }`
- `TextMeasureCallback`
- `TextMeasureProvider`
- `measure_text(...)`
- `fallback_text_metrics(...)`

`LayoutEngine` 接受可选 provider：

```cpp
LayoutEngine layout_engine(style_resolver, TextMeasureProvider{measure, context});
```

回调收到 UTF-8 文本、CSS font-size 和 CSS font-weight，返回未换行文本段的宽度和单行高度。
layout engine 会用这个宽度在可用内容宽度内估算换行。

`src/core/software_renderer.h` 仍然负责绘制侧回调：

- `TextPainter`
- `TextPaintCallback`

文本 display command 现在带有最小绘制语义：

- 水平对齐：start、center 或 end；
- 单行文本或可换行文本。

重视视觉正确性的宿主应让测量和绘制来自同一个字体引擎。二者不一致时，文本可能被裁切，或换行位置与实际绘制不一致。

`src/core/text_adapter.h` 提供 `HostTextAdapter`，用于桥接已经同时提供测量和绘制能力的
LVGL 或厂商字体引擎。adapter 不持有资源；宿主拥有的 context 必须覆盖 layout 和 render 生命周期：

```cpp
HostTextAdapter adapter{measure_text, paint_text, host_font_context};
LayoutEngine layout(style_resolver, text_measure_provider_from_adapter(adapter));
SoftwareCompositor compositor(text_painter_from_adapter(adapter));
```

这个 helper 只是为了让板级 port 的接入形态一致，不会把字体发现、shaping 或 cache 放进核心。

## Fallback 行为

内置测量 fallback：

- 按 UTF-8 码点遍历，而不是按字节遍历；
- ASCII 近似为 `2/3 * font-size`；
- 非 ASCII 近似为 1em；
- 对非空文本增加少量右侧安全余量；
- 对粗体文本略微增加宽度。

内置绘制 fallback：

- 用很小的 5x7 bitmap 字形绘制 ASCII 字母、数字和常用标点；
- 每个非 ASCII 码点绘制一个占位字形；
- 通过第二次 stroke pass 近似粗体。

这能让核心保持确定、很小、容易移植。它有意不实现字体发现、glyph cache、复杂 shaping、双向文本、kerning 或完整 Unicode fallback。

## Win32 验证后端

`examples/win32_browser.cpp` 现在同时为 layout 测量和文本绘制注入 GDI。
桌面截图和交互式验证壳因此会更接近真实显示效果，尤其是中文和其他 UTF-8 文本。

这个后端不是嵌入式核心的一部分，只属于桌面验证壳。

## 嵌入式接入建议

好的嵌入式文本后端应当：

- 让字体资源由宿主或板级支持包持有；
- 测量函数和绘制函数使用同一份字体数据；
- 避免在逐帧测量和绘制回调中分配内存；
- 只有在目标内存足够且 invalidation 模型清楚时，才在宿主侧缓存 shaped/measured string；
- 尽可能选择固定 UI 字体集合。

推荐选项：

- 面向 ASCII/数字手表 UI 的静态 bitmap font atlas；
- LVGL 文本测量和绘制 bridge；
- 面向中文产品的厂商字体引擎；
- 只有确实需要复杂文字系统时，才接入 shaping-capable backend。

`src/core/bitmap_font.h` 提供第一版静态 bitmap font backend：

- `BitmapFontGlyph`：一个按 Unicode codepoint 寻址的单色 glyph；
- `BitmapFont`：glyph table、line-height 和 fallback advance。glyph table 必须按 Unicode codepoint 升序排列，因为大 CJK 字库查找使用二分搜索；
- `BitmapFontContext`：选择的 font 和整数 scale；
- `bitmap_font_measure_callback`；
- `bitmap_font_paint_callback`。

生成的嵌入式 font pack 应接近普通 C++ 数据：

```cpp
static constexpr std::uint8_t rows_digit_0[] = { /* row bitmasks */ };
static constexpr BitmapFontGlyph glyphs[] = {
    BitmapFontGlyph{0x30, 5, 7, 6, 1, rows_digit_0},
};
static constexpr BitmapFont font{glyphs, 1, 8, 6};
static BitmapFontContext font_context{&font, 1};
```

同一个 `font_context` 应同时传给 `TextMeasureProvider` 和 `TextPainter`。

`jellyframe_font_pack_gen` 可以从 BDF bitmap 字体生成这个结构：

```text
jellyframe_font_pack_gen --bdf font.bdf --chars used_chars.txt --output font_pack.h --name app_font
```

生成器通过 `BitmapFontGlyph::bytes_per_row` 支持宽度超过 8px 的 glyph，这是实用中文 bitmap
字体必须具备的能力。

## 字体子集

`jellyframe_capability_check` 可以辅助准备嵌入式字体包：

```text
jellyframe_capability_check --emit-used-chars used_chars.txt app.html app.css app.js
jellyframe_capability_check --font-coverage font_chars.txt app.html app.css app.js
jellyframe_capability_check --font-budget 16x16 app.html app.css app.js
```

`used_chars.txt` 会包含源码中出现的非 ASCII UTF-8 字符，并识别常见 numeric/named character references。
`font_chars.txt` 是一个普通 UTF-8 文本文件，列出嵌入式 font pack 已提供的字符。部署前，工具会报告缺失的非 ASCII codepoints。
`--font-budget WxH` 会根据非 ASCII 子集输出粗略 bitmap font pack byte 估算，
便于决定是否纳入更大的中文字符集。工具也会输出字体 profile 建议：

- `tiny`：ASCII、数字和基础 UI 符号足够，适合 bring-up 或纯英文 UI。
- `tiny-plus-symbols`：保留 tiny 字体，只追加扫描到的非 ASCII 符号。
- `app-subset-cn`：应用只使用少量中文，建议生成 app-specific 字体包。
- `cn-standard`：面向国内中文设备时，推荐 ASCII + 常用符号 + GB2312 一级汉字作为可复用 profile。
- `global-product`：检测到混合或非中文脚本，应按市场选择 Latin Extended、Greek、Cyrillic、Kana、
  Hangul 等子集，而不是放入一个全球大而全字体。

不要把 `cn-standard` 当作 JellyFrame 的全球默认字体。它是中文市场推荐预设。flash 紧张的产品应优先使用
app-specific subset；全球化产品应按销售区域提供字体包。

预期生产路径：

1. 作者正常编写 HTML/CSS/JS 文本。
2. 用能力验证器收集需要的非 ASCII 字符。
3. 用离线字体工具从授权矢量字体 rasterize 这些 glyph，生成静态嵌入式 bitmap font pack。
4. 宿主文本后端链接这个 font pack。

## 当前限制

- 文本 layout 仍是简化的 block/inline wrapping，不是完整浏览器 inline formatting context。
- core 不加载字体，也不实现 font-family cascade。
- 不支持 HarfBuzz 级 shaping、双向文本、连字、kerning 或 hyphenation。
- 当前回调返回整段文本 metrics；按单词、grapheme 的精细换行仍是后续工作。

对于可穿戴应用，这已经足够支撑 label、button、form、calculator、timer、weather panel
和简单文章式文本。正式多语言产品应先提供平台文本后端，再评价布局质量。
