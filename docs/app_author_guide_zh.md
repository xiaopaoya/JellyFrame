# App 作者手册

> 最后更新：2026-07-07；适用版本：0.5.0-dev

这是一份给 JellyFrame app 作者的短契约。JellyFrame 不是迷你浏览器，而是一个 Web 形状的嵌入式
UI runtime：HTML 负责结构，CSS 使用文档化的小屏样式子集，JavaScript 负责有界本地交互，manifest
声明 app 需要的目标设备和宿主服务。

如果按“任意网页”来写，会很快撞到限制。如果按“小型可穿戴 app + 明确 target + 明确预算”来写，
工具链就能帮你做出可预测的 UI。

## 从这里开始

每个 app 都建议使用这个循环：

```powershell
python tools\jellyframe_cli.py new `
  --template weather `
  --output build\my_weather `
  --id org.example.weather `
  --name Weather `
  --target round-300

python tools\jellyframe_cli.py check `
  --root build\my_weather `
  --target round-300 `
  --targets round-300,rect-320x240,rect-172x320 `
  --report build\my_weather.report.json `
  --build-dir build\Release

.\build\Release\jellyframe_win32_browser.exe --app build\my_weather
```

然后优先读 JSON report 末尾的 `developerAdvice[]`。它是低层 diagnostics 面向 app 作者的解释和修复建议。
部分建议会带 `recipe` 字段，指向 [app_author_recipes_zh.md](app_author_recipes_zh.md)
中的可复制写法。

如果页面感觉卡顿，下一步看 `performanceSummary` 和 `performanceAdvice[]`。这些字段会量化
预检阶段能可靠判断的复杂度：DOM/render/layout 对象数量、layer 与 display command 数量、
framebuffer bytes、估算 pipeline heap、资源预算占比和 full-frame present 规模。`check` 调用
伪浏览器时，summary 还会带桌面工具侧阶段耗时，例如 parse、layout、paint、present 的微秒数。
这些耗时适合判断“时间花在哪一步”，但仍不是设备 FPS。真实帧耗时、DMA wait 和 panel flush 时间
仍应通过 Win32 frame-script capture 或设备 telemetry 获取。

诊断标题和解释会尽量复用 Web/CSS 规范中已有的表达：parse error、invalid declaration、
unsupported value、overflow、clipping、deferred API 等。JellyFrame 自己的 `code` 字段只作为
稳定的机器可读标识，便于工具和 CI 使用。

## 现在可以依赖什么

目前比较稳定的作者能力：

- 本地 package HTML、CSS 和 classic JavaScript。
- 简单 DOM 结构、ID/class、文本、表单和按钮。
- Block/inline layout、简化 flex、实用 grid-card 子集、小装饰用 absolute positioning 和固定底部栏。
- `box-sizing: border-box`、百分比 sizing、`max-width: 100%`、`gap`、`aspect-ratio`、
  单值百分比 `border-radius`、轻量渐变、圆角矩形、近似阴影，以及声明后可用的 Canvas 2D V0.3。
- DOM event、click/pointer/touch alias、focus、基础表单状态、timer 和有界 `requestAnimationFrame`。
- manifest 与 host target 同时允许时的 XHR GET、极小 `localStorage`、Audio V0 和 geolocation V0。
- Win32 壳中的包内 BMP 图片，以及产品 PNG/JPEG/WebP decoder 可接入的 host image codec adapter 边界。
- App `.jffont` 补充字体和默认字体子集预检。

依赖细节前，请查完整能力矩阵：
[developer_capability_matrix_zh.md](developer_capability_matrix_zh.md)。
只想快速判断“能不能用”时，先看
[app_author_capability_table_zh.md](app_author_capability_table_zh.md)。
需要可复制的小屏组件写法时，读
[app_author_recipes_zh.md](app_author_recipes_zh.md)。

## 不要默认以为能用

不要按浏览器直觉假设这些能力存在：

- 远程 HTML/CSS/script/image 加载。
- 浏览器级 `fetch`、cookie、IndexedDB、Cache API 或通用文件系统。
- 完整 CSS Grid/Flexbox、selector engine、pseudo-elements、filter、SVG、完整 Canvas 或视频。
- 浏览器字体匹配、`@font-face`、矢量 shaping、italic/style/stretch 自动匹配或全局字体兜底。
- JavaScript 里出现某个 API 名称就代表硬件服务一定可用。
- 像手机浏览器一样自然的整页滚动模型。

JellyFrame app 是本地、有界、target-aware 的 UI 包。

## Manifest 规则

Manifest 不是样板文件，而是 app 契约的一部分。

- 在 `capabilities` 中声明服务：`network.fetch`、`storage.kv`、`media.audio.playback`、
  `location.position`、`graphics.canvas2d` 和文档化 sensor 名称。
- 声明 capability 只是请求，不是保证。所选 target profile 和产品宿主也必须提供同一服务。
- 用 `targets[id].gate` 表达发布门槛。app 宣称支持的设备用 `reject`；仍在试验或可选的设备用 `warn`。
- 预算要真实。没有测量就提高预算，通常只是把失败挪到设备上。
- CSS 使用自定义 `font-family` 时，要么添加匹配 family 的 `.jffont` manifest 条目，要么使用
  `system-ui` / `sans-serif`。

## 小屏布局 recipes

可穿戴 app 的默认做法：

- 先为 `300x300` 圆屏设计第一屏，再验证 `320x240` 和 `172x320`。
- 全局使用 `box-sizing: border-box`。
- card、row、image 和 canvas 使用 `max-width: 100%`。
- 窄屏优先使用纵向 stack。
- label 保持短小。使用 `Hourly`、`Daily`、`Air` 这类短词，避免长 tab 文本。
- 长内容放进一个明确的 `overflow: auto` 容器。
- 固定底部导航放在 scroll container 外。
- 避免在 `172x320` 上放很多横向按钮。
- 用 `@media (max-width: ...)` 和 `@media (max-height: ...)` 缩减 padding、card 数、列数和字号。
- 重复 card 用 `gap`，不要靠一堆 margin 堆间距。

最小骨架：

```css
* {
  box-sizing: border-box;
}

body {
  margin: 0;
  width: 100%;
  height: 100%;
  font-family: system-ui, sans-serif;
}

.screen {
  width: 100%;
  height: 100%;
  padding: 14px;
}

.stack {
  display: grid;
  grid-template-columns: 1fr;
  gap: 10px;
}

.card {
  max-width: 100%;
  border-radius: 18px;
  padding: 12px;
}

@media (max-width: 200px) {
  .screen {
    padding: 8px;
  }

  .card {
    padding: 9px;
  }
}
```

更完整的按钮、卡片、滚动列表和固定底部导航写法见
[app_author_recipes_zh.md](app_author_recipes_zh.md)，活样例位于
`samples/apps/packages/jelly_component_recipes`。

## 常见 warning 怎么修

| Diagnostic | 通常含义 | 优先修法 |
| --- | --- | --- |
| `layout-text-overflow` | 文本放不进盒子。报告通常包含 `text`、`node`、`path`、测量宽度和可用宽度。 | 缩短 label、加宽盒子、减小字号，或为窄屏写 media rule。 |
| `visual-horizontal-overflow` | 绘制内容超出 target 宽度。报告包含 `paintBounds`、viewport 和越界像素；如果能归因到具体布局盒，还会给出 `node`、`path` 和 `boxOverflow*` 字段。 | 加 `max-width: 100%`、用 `box-sizing: border-box`、改纵向布局，或把长内容放进 scroll container。 |
| `visual-scroll-needed` | 页面比 viewport 高。 | 判断是否需要滚动。需要就使用明确 `overflow: auto` 区域并在 target gate 允许滚动。 |
| `visual-scroll-container` | 内部滚动区域裁切了内容。报告通常包含 `node`、`path`、`boxHeight`、`contentHeight` 和 `overflowY`。 | 确认报告中的容器可被触摸/滚轮/按键滚动，并把固定导航放在滚动容器外。 |
| `font-family-unmatched` | CSS 自定义字体没有对应 manifest 字体。 | 使用 `system-ui`，或声明匹配 family 的 `.jffont`。 |
| `font-missing-glyphs` | target 字体不覆盖 app 文本。 | 用生成的 `*.used_chars.txt` 制作并声明 app 字体补充包。 |
| `style-property-unsupported` | CSS 属性不在支持子集。 | 换成文档化属性，或用 Canvas/资源图表达效果。 |
| `target-gate-not-accepted` | app 声明的 target 没过发布 gate。 | 修复列出的 overflow/scroll/diagnostic 原因；仍试验时可先把 gate 降为 `warn`。 |

## 什么时候用 Canvas

Canvas 2D V0.3 适合自定义仪表、圆环、小图表和用 DOM 很难低成本表达的装饰。使用前声明
`graphics.canvas2d`，并保持 canvas 尺寸小。除非 app 本身就是图形优先，否则不要用 Canvas 重写整个 UI。

## 分享 app 前

运行：

```powershell
python tools\jellyframe_cli.py check `
  --root your_app `
  --target round-300 `
  --targets round-300,rect-320x240,rect-172x320 `
  --report build\your_app.report.json `
  --build-dir build\Release
```

然后确认：

- `developerAdvice[]` 为空，或只包含你明确接受的取舍。
- 支持的 target 都是 `gate.decision: "accept"`。
- Win32 壳中没有意外文本裁剪。
- 字体报告覆盖所有可见字符。
- JavaScript 使用的宿主服务已经在 manifest 声明，并被 target profile 支持。
