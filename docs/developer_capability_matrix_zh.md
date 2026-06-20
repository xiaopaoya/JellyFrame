# 开发者能力矩阵


这份文档是 JellyFrame 面向应用开发者的实际能力契约。开发者在使用某个 HTML
标签、CSS 属性、DOM/JS API、事件或渲染能力前，应能通过这里判断：它现在能不能工作、
会如何降级、是否只是被解析保存、是否会被懒处理或直接跳过。

JellyFrame 不是通用浏览器。它是一个为可穿戴/嵌入式 UI 准备的小型
HTML/CSS/DOM/script runtime，目标是保留“像写小网页一样写应用”的开发模型，同时裁剪
网络、完整浏览器加载、GPU、复杂字体、完整 CSS layout 等高成本能力。

## 状态说明

- **可用**：已经实现，属于当前建议使用范围。
- **子集**：能用，但只应依赖这里写明的子集。
- **已保存**：会被解析或保存在内部结构里，但视觉/行为未完整执行。
- **懒处理**：整体跳过或简化，不应破坏后续解析和渲染。
- **延后**：刻意不支持；不要依赖。
- **壳层限定**：只存在于桌面例程或 Win32 验证壳，不属于平台无关核心。

## 最适合的项目

适合：

- 天气、时钟、计时器、计算器、设置页。
- 卡片式小仪表盘、本地配置界面、表单型设备 UI。
- 想用 HTML/CSS/JS 编写嵌入式应用，而不是用 canvas 手绘全部 UI 的项目。
- 使用 `jellyframe_pseudo_browser` 或 `jellyframe_win32_browser` 做桌面验收。

暂不适合：

- 任意现代网站。
- 假设完整 DOM、selector API、浏览器 loader、网络、存储、模块、canvas 或 Web Components
  的前端框架。
- 像素级兼容浏览器渲染。
- 依赖完整 flex/grid、container query、图片解码、字体加载、复杂文字 shaping 的大页面。

## 核心边界

| 能力 | 状态 | 行为 |
| --- | --- | --- |
| 平台无关核心 | 可用 | 核心不做文件、网络、窗口、硬件 I/O。 |
| 伪浏览器 | 壳层限定 | 运行完整管线并写出 BMP/PPM。默认使用极小内置字体；未注入平台文本绘制时中文会显示为 fallback glyph。 |
| Win32 browser 壳 | 壳层限定 | 打开桌面窗口，使用 GDI 文本测量/绘制，转发鼠标/滚轮/键盘输入，支持截图输出和可选 scripting 构建。 |
| 嵌入式后端 | 延后 | 最终显示、触摸、按键、刷新策略应由目标平台接入。 |
| 外链 CSS 加载 | 壳层限定 | 示例工具可读取本地 `<link rel="stylesheet">`；核心只提供 callback 辅助。 |
| 网络 | 宿主可选 XHR V0 | 核心仍无 HTTP、WebSocket 或远程资源加载，不允许远程 HTML/CSS/script/image 进入页面 loader。`NetworkFetchMock` 已提供 fixture/handle/completion 契约；`JerryScriptRuntime` 在 scripting 构建中暴露 async `XMLHttpRequest` GET 子集；真实网络由宿主 service/worker 完成，JS callback 只在 UI/main task pump completion 后执行。 |
| 存储 | 宿主可选 localStorage V0 | 无 cookie、IndexedDB 或核心文件系统 API。`AppPrivateKvStorageMock` 已提供 app id 隔离的异步 KV 契约和预算检查；`JerryScriptRuntime` 在宿主绑定非阻塞 `AppLocalStorageShadow` 时暴露极小 `localStorage` 子集，未绑定时不暴露。 |
| 系统状态事件 | 宿主可选 V0 queue | `AppSystemEventQueue` 允许宿主为当前 app instance 注入有界的时间/时区/网络/电量/屏幕/低功耗状态快照。旧实例事件会在帧边界丢弃；`try_push_current(...)` 可诊断 `empty-instance` / `queue-full`。JerryScript V0 已映射 `navigator.onLine`、`document.hidden`、`document.visibilityState` 和 `visibilitychange`。 |

## HTML 解析

| 功能 | 状态 | 行为 |
| --- | --- | --- |
| UTF-8 输入 | 可用 | 解析按字节/字符串处理。最终文字显示质量取决于文本后端。 |
| 开始/结束标签 | 可用 | 常用标签会生成 DOM 元素。 |
| 属性 | 可用 | 支持常见引号形式和未加引号形式。HTML 路径会规范化属性名。 |
| 文本节点 | 可用 | DOM 文本保留作者空白。Render tree 会跳过非保留上下文中的纯格式化空白文本，避免缩进换行污染 block/grid/flex layout；Layout/rendering 会折叠普通显示文本；`pre`、`script`、`style`、`textarea` 和 `title` 保留文本。 |
| 注释 | 可用 | tokenizer 可处理，视觉树忽略。 |
| Doctype | 可用/懒处理 | 接受但不进入 quirks mode。 |
| 字符引用 | 子集 | 常见 named references 和十进制/十六进制 numeric references 会解码，并包含常见 Windows-1252 legacy numeric remap；未知情况按字面或 fallback 处理。 |
| `script`/`style` raw text | 子集 | 足够支持样式/脚本收集。 |
| `textarea`/`title` RCDATA-like | 子集 | 在有界简化内容扫描中解码字符引用；不提供完整浏览器 RCDATA 状态兼容。 |
| 自动合成 `html`/`body` | 可用 | 缺失外层结构会被修复。 |
| void elements | 可用 | 常见 void 标签不要求闭合。 |
| 隐式闭合 | 子集 | 容忍常见段落/列表/table-ish 情况；不追求完整 HTML tree builder 兼容。 |
| 错误恢复 | 子集 | 有明确资源上限，并可报告 node/depth/attribute 预算诊断；应避免死循环和崩溃。 |
| Quirks mode | 延后 | 完全抛弃。JellyFrame 只面向现代作者写法。 |
| `template` | 懒处理 | 默认样式隐藏；template 内容语义未实现。 |
| 自定义元素 | 子集 | 未知标签可成为普通元素并被样式化；无生命周期回调。 |

## DOM 模型

| 功能/API | 状态 | 行为 |
| --- | --- | --- |
| `Node` tree | 可用 | Element/Text 两类节点，带 parent/children 所有权。 |
| `tag_name`、`text`、`attributes` | 可用 | C++ 内部模型直接保存。 |
| `append_child` | 可用 | 挂载/移动子节点，并标记 tree/layout dirty。 |
| `detach_child` / `remove_child` | 可用 | 移除子节点所有权，并标记 tree/layout dirty。 |
| `set_attribute` / `remove_attribute` | 可用 | 更新属性，必要时重置表单状态，并标记 attributes/style/layout dirty。 |
| `set_text` / `set_text_content` | 可用 | 内容变化时标记 text/layout dirty；同值设置不会制造脏标记。 |
| `text_content()` | 可用 | 拼接后代文本。 |
| `attribute()` | 可用 | 缺失属性返回空字符串。 |
| `has_class()` | 可用 | 按空白分隔 class token。 |
| Dirty flags | 可用 | dirty 位向祖先传播，因此根节点 dirty 检查为 O(1)；清理时跳过干净子树。 |
| DOM Range/Selection | 延后 | 暂无 Range、Selection。 |
| MutationObserver | 延后 | 使用宿主 dirty flags 观察变化。 |
| Shadow DOM | 延后 | 无 shadow root、slot、part、scoped tree。 |
| 完整浏览器 `document` | 延后 | 只有 scripting 绑定里的小型子集。 |

## CSS Syntax 与 CSSOM

| 功能 | 状态 | 行为 |
| --- | --- | --- |
| 注释 | 可用 | 解析时移除。 |
| 普通规则 | 可用 | `selector { declarations }`。 |
| selector list | 可用 | 顶层逗号拆分。 |
| declaration 顺序 | 可用 | 保留重复属性，用于 fallback。 |
| `!important` | 可用 | 参与 cascade。 |
| 函数/字符串/括号组件 | 可用 | 平衡跳过，避免坏值破坏后续规则。 |
| 错误恢复 | 可用 | 在 declaration/rule 边界恢复。 |
| `@layer` | 懒处理 | block 被展开；不建模 layer ordering。 |
| `@media` | 子集 | 空、`all`、`screen` block 会解析。由 `screen`/`all` 加 `min-width`、`max-width`、`min-height`、`max-height` 组成的查询会按 parser viewport 求值，条件值支持 `px` 和无单位 px-like 数字。不支持或复杂的 media query 会整体跳过。 |
| `@supports` | 子集 | 保守求值 declaration feature query。支持 `(property: value)`、`not`、同质 `and`/`or` 链和括号；`selector()` 以及未知或不安全特性会求值为 false 并跳过 block。 |
| `@container` | 延后/懒处理 | 整个 block 跳过。不要把必需 UI 放在里面。 |
| `@font-face` | 懒处理 | 平衡跳过；当前不按 CSS 字体规则加载字体文件，`.jfapp` 中的 font 资源也不会因 `font-family` 自动影响文本后端。`.jffont` 可通过 Win32 壳 `--use-app-fonts` 显式验收。 |
| 字体覆盖检查 | 默认工具预检 | `package`、`check`、`preview` 和源码包 `install` 默认运行 `jellyframe_font_resource_check`；`--font-coverage` 在嵌入前报告缺失 codepoints，`--no-font-check` 可显式跳过。Package report 还会输出 `fontDiagnostics`，合并源码 codepoints、目标字体 profile 估算和 manifest `.jffont` glyph table，在安装前报告 app 可见缺字。 |
| 字体 profile 与预算估算 | 默认工具预检 | 默认按 `16x16` 估算 bitmap font pack bytes；`--font-budget WxH` 可调整，并根据扫描到的码点建议 `tiny`、`tiny-plus-symbols`、`app-subset-cn`、`cn-standard` 或 `global-product`。 |
| Bitmap font pack 生成 | 工具/runtime/fallback 链可用 | `jellyframe_font_pack_gen` 把 BDF bitmap 字体裁剪成嵌入式构建可用的 C++ `BitmapFont` header，也可输出 `.jffont` V0 二进制补充包；`BitmapFontResource` 可解析 `.jffont` bytes，`AppFontSet` 已提供 bitmap fallback chain：系统字体 profile 优先，app `.jffont` supplement 补缺字。Win32 壳 `--use-app-fonts` 可让包内字体参与 layout/paint 验收。稳定的 `.jfapp`/flash 字体 payload 可通过 zero-copy view 路径挂载。CSS family 匹配和多字号字体包仍是后续工作。 |
| `@keyframes` | 子集 | 解析命名 `@keyframes` block，并保存 `from`/`to` 或 `0%`/`100%` declaration。中间百分比会诊断并忽略。执行范围受下方 animation 属性子集限制。 |
| 未知 at-rule | 懒处理 | 跳过 statement 或平衡 block。 |
| CSS custom properties | 子集 | 支持直接 `var(--token)` 和 `var(--token, fallback)`，来源包括继承的 `:root`、祖先、当前元素和 inline custom property declarations。无法解析的 `var()` 不会覆盖之前的受支持 fallback。完整依赖图、区分大小写的 custom property 名称和完整 invalid-at-computed-value-time 语义尚未实现。 |
| CSS nesting | 延后 | 不要依赖嵌套 selector。 |
| Cascade origins | 子集 | author + inline + 小型内置默认样式；无 user/animation origin。 |
| Rule indexing | 可用 | 按最右侧 id/class/tag/universal 建桶。 |

## Selectors

| Selector | 状态 | 行为 |
| --- | --- | --- |
| Type selector | 可用 | `button`、`section`。 |
| Class selector | 可用 | `.card`。 |
| ID selector | 可用 | `#search`。 |
| 简单 compound | 可用 | `button.primary.large`。 |
| Descendant combinator | 可用 | `.panel button`。 |
| Child combinator | 可用 | `main > section`。 |
| 简单 attribute selector | 子集 | 支持存在性和简单等值类匹配。 |
| `:root` | 可用 | 支持。 |
| 动态 pseudo-class | 子集 | `:hover`、`:active`、`:focus`、`:focus-within`、`:checked` 和 `:disabled` 会参与 selector matching。输入状态变化会标记 style/layout dirty；checked/disabled 来自表单控件和属性状态。 |
| `:is()` / `:where()` | 子集 | `:is()` 匹配 selector-list 参数，并贡献参数中的最高 specificity。`:where()` 匹配同一子集，但 specificity 为 0。 |
| `:has()` | 延后/懒处理 | 含 `:has()` 的规则会跳过；关系型 selector matching 刻意延后。 |
| Pseudo-elements | 子集 | `::before` 支持极小的 generated-content 路径，可用于文本/counter 列表标记。`::after`、完整 marker 样式和 selection 样式延后。 |
| Sibling combinators | 子集 | Adjacent `+` 和 general `~` sibling selectors 会匹配前序元素兄弟。元素之间的文本节点不会阻断 adjacent matching。 |
| Shadow selectors | 延后 | `::part`、`::slotted` 跳过。 |

## CSS 属性

只应把下表支持的值用于必需 UI。不支持的值不会覆盖之前已经支持的 fallback。

| 属性 | 状态 | 支持值/降级 |
| --- | --- | --- |
| `display` | 子集 | `block`、`inline`、`inline-block`、`flex`、`inline-flex`、`grid`、`inline-grid`、`none`。inline flex/grid 映射为同一简化布局模式。 |
| `color` | 子集 | 基础命名色、hex、`rgb()`、`rgba()`。`oklch()` 等不覆盖 fallback。 |
| `background-color` | 子集 | 与 `color` 相同的颜色解析；刻意不接受渐变，因为 CSS 语义中渐变属于背景图像。 |
| `background` | 子集 | 支持纯色，以及 `linear-gradient(<color>, <color>)`、`linear-gradient(to bottom/top/right/left, ...)` 这类低成本两色线性渐变绘制命令。图片、复杂 stop 和角度会被忽略或诊断，不会覆盖之前 fallback。 |
| `margin` | 可用 | 1-4 个长度值，支持水平 `auto`。 |
| `margin-top/right/bottom/left` | 可用 | 物理 longhand。`margin-left/right:auto` 可用于当前水平居中路径。 |
| `padding` | 可用 | 1-4 个长度值。 |
| `padding-top/right/bottom/left` | 可用 | 物理 longhand。 |
| `border` | 子集 | 支持 `none`，从简单 shorthand 中提取 width 和 color；style 关键词只作为可忽略文本。 |
| `border-width` | 可用 | 1-4 个长度值。 |
| `border-top/right/bottom/left-width` | 可用 | 物理边框宽度 longhand。 |
| `border-color` | 子集 | 单色应用到所有边。 |
| `border-radius` | 子集 | 单个长度 radius。支持圆角填充；复杂四角 radius 不支持。 |
| `outline` / `outline-width` / `outline-color` | 子集 | 作为不参与布局的外扩 stroke 绘制。支持简单 width/color shorthand；`outline-offset` 和复杂 style 语义延后。 |
| `width` / `height` | 可用 | 支持单位的长度值。 |
| `min-width` / `min-height` | 可用 | 长度值。 |
| `max-width` | 可用 | 长度值；block layout 使用。 |
| `aspect-ratio` | 可用 | 正数或 `w / h`，包括 `auto w / h`。用于 intrinsic box height。 |
| `font-size` | 可用 | 长度值。 |
| `font-weight` | 子集 | `normal`、`bold`、`bolder`、`lighter` 和数字权重。软件 fallback 近似加粗；最终字重由平台 text painter 决定。 |
| `line-height` | 可用 | 无单位倍率或长度。 |
| `text-align` | 可用 | `left`、`right`、`start`、`end`、`center`。 |
| `text-indent` | 可用 | 长度值。 |
| `text-decoration` / `text-decoration-line` | 子集 | `none`、`underline`、`line-through` 会绘制便宜的实线装饰。颜色/粗细/style 变体和 wavy/double 延后。 |
| `text-shadow` | 子集 | 第一条 shadow 会绘制为偏移文本；blur 只用于解析兼容，不做真实模糊；多重阴影暂不栅格化。 |
| `box-sizing` | 可用 | `content-box`、`border-box`。 |
| `overflow` | 子集 | `visible`、`hidden`、`clip`、`auto`、`scroll`；会形成裁剪 layer，但原生滚动容器不完整。 |
| `opacity` | 子集 | 0..1；软件合成中创建 composited layer。 |
| `position` | 子集 | `relative` 只做视觉偏移，不改变普通流占位。`absolute`/`fixed` box 会脱离普通流，并用简单 inset 定位。`sticky` 目前只作为 layer hint 保存。 |
| `top` / `right` / `bottom` / `left` | 子集 | 支持长度和 `auto`。Absolute/fixed box 使用父内容框或近似 viewport 原点。百分比、shrink-to-fit、完整 containing-block 规则和 sticky 滚动行为不支持。 |
| `z-index` | 子集 | 整数或 `auto`；目前是 layer-local ordering。 |
| `transform` | 已保存/懒处理 | 非 `none` 创建合成边界；矩阵变换延后。 |
| `justify-content` | 子集 | `start`、`flex-start`、`normal`、`center`、`space-around`、`space-between`，用于简化 flex row。 |
| `align-items` | 子集 | `stretch`、`normal`、`start`、`flex-start`、`center`、`end`、`flex-end`，用于简化 flex row。 |
| `flex` | 子集 | Shorthand 支持常见 `none`、`auto`、`<grow>`、`<grow> <basis>` 和 `<grow> <shrink> <basis>` 形式，用于简化 row flex layout。完整 Flexbox 语法不支持。 |
| `flex-grow` / `flex-shrink` / `flex-basis` | 子集 | 非负数字 grow/shrink 因子和受支持长度/`auto` basis 会参与简化 row sizing pass。 |
| `flex-wrap` | 子集 | `wrap`/`wrap-reverse` 启用简单行换行。换行后只做固定/basis 探测，不执行完整逐行 Flexbox 算法。 |
| `gap` | 可用 | 1-2 个长度值，用于 grid 和简化 flex。 |
| `row-gap` / `column-gap` | 可用 | 长度值。 |
| `grid-template-columns` | 子集 | 从 `repeat(auto-fit, minmax(<length>, 1fr))`、`minmax(<length>, 1fr)`、单个长度或 `1fr` 中提取最小轨道。 |
| 简单固定 grid 列 | 子集 | 支持 `grid-template-columns: <length> 1fr`、`repeat(N, 1fr)`、`repeat(N, minmax(0, 1fr))` 及相近的 2-4 列 length/`fr` 模板，适合描述列表、设置表单和紧凑键盘。 |
| `grid-auto-rows` | 子集 | 长度或 `minmax(<length>, auto)` 最小行高。 |
| `grid-column` / `grid-row` | 子集 | `span N`，内部有界钳制。无显式 line placement。 |
| `list-style` / `list-style-type` | 子集 | `none`、disc-like 和 decimal-like 值。`li` 会绘制轻量原生列表标记。 |
| `content` on `::before` | 子集 | 支持纯文本和 `counter(name) "suffix"`，用于轻量列表计数。完整 generated-content layout 延后。 |
| `box-shadow` | 子集 | 第一条 shadow 近似为圆角半透明填充。不做真实 blur 和多重阴影。 |
| `object-fit` | 子集 | 支持 `fill`、`contain`、`cover`、`none`、`scale-down`，默认按居中位置绘制。复杂 `object-position` 延后。 |
| `font-family` | 延后 | 实际字体由文本后端决定。Win32 壳使用 Microsoft YaHei UI。 |
| `requestAnimationFrame` | 脚本子集 | JerryScript 构建中可用。宿主按每帧预算和时间戳泵动 callback。后台/低功耗 profile 可把 animation callback/FPS 预算设为 0。 |
| CSS `transition` | 子集 | 支持 `transition` 和 `transition-*` 的有界列表，当前可动画属性为 `opacity`、`transform: translate()/scale()`、`background-color` 和 `color`。Win32 调试壳会在交互状态变化时推进 timeline，并用 animation dirty-region helper 只重绘前后运动/绘制区域；layout 属性动画不做逐帧重排。 |
| `@keyframes` / `animation-*` | 子集 | 支持有界 `animation`、`animation-name`、`animation-duration`、`animation-delay`、`animation-timing-function`、`animation-iteration-count` 和 `animation-direction`。执行的 keyframes 仅限 `from`/`to`，属性限于 `opacity`、`transform: translate()/scale()`、`background-color` 和 `color`；`width`、margin、grid/flex 等 layout 属性会诊断并忽略，不做逐帧 reflow。支持 `normal`/`alternate` direction，以及正整数或 `infinite` iteration count。不支持 fill-mode、play-state 和多个百分比关键帧插值。 |
| filter/backdrop-filter | 延后 | 不绘制。 |

当前长度单位包括 `px`、无单位 px-like 数字、`rem`、`em`、简化 `vh`/`vw`，
以及基于默认紧凑 viewport 宽度的百分比 fallback。`min()`、`max()`、`clamp()` 和简单
`calc(A +/- B)` 会在参数能归约为受支持长度时解析。这是保守 fallback，不是完整 CSS value algebra。

## Layout

| 功能 | 状态 | 行为 |
| --- | --- | --- |
| Block layout | 可用 | 垂直盒模型，支持 margin、padding、border、max-width、水平 auto margin。 |
| Inline text flow | 子集 | 文本和 inline 控件横向流动，并按可用宽度换行。 |
| Inline 背景/边框 | 子集 | 尽量收缩到文本/内容范围。 |
| `inline-block` | 子集 | 表示为 inline-like render object，并具备可用盒行为。 |
| Flex row | 子集 | 简化 row，支持基础 grow/shrink/basis sizing、justify、align、gap 和可选 wrapping。无完整 Flexbox 算法、column flex、order、baseline alignment 或 min-content sizing。 |
| Grid cards/forms | 子集 | 响应式 auto-fit/minmax 卡片 grid、gap、最小 auto rows、span、`repeat(N, 1fr)`、`repeat(N, minmax(0, 1fr))` 和简单固定 2-4 列模板。无显式 placement、named lines、subgrid、dense packing。 |
| Aspect ratio | 可用 | 没有显式高度/内容高度时提供 intrinsic height。 |
| Positioned boxes | 子集 | 有界支持 `relative`、`absolute` 和 `fixed` 定位，适合 app overlay、角标和固定面板。Out-of-flow box 不占 block/flex/grid/inline placement 空间。 |
| Replaced elements | 子集 | 常见控件/媒体作为 leaf boxes，有 fallback 尺寸；真实 image/video layout 延后。 |
| 文本测量 | 子集 | core 暴露 `TextMeasureProvider`；fallback 很小但按 UTF-8 码点估算。Win32 壳使用 GDI 测量。`HostTextAdapter` 可包装 LVGL/vendor 测量 callback。 |
| Bidi/text shaping | 延后 | 生产级非拉丁文本需要平台 text backend 或后续 shaping 策略。 |
| Fragmentation/multicolumn | 延后 | 未实现。 |

## 表单控件

| 元素/功能 | 状态 | 行为 |
| --- | --- | --- |
| `button` | 可用 | 轻量原生风格绘制，默认近似按内容收缩，支持 click。 |
| `input type=text` 和默认 input | 可用 | 有 value 状态，宿主可输入 UTF-8 文本，支持 Backspace。 |
| `input list` / `datalist` | 子集 | 不显示原生 popup。获得焦点的文本输入框可用 Tab/Enter 接受第一个匹配的 datalist option。 |
| `textarea` | 子集 | value-like 状态和基础绘制；完整多行编辑有限。 |
| `input type=checkbox` | 可用 | checked 状态、点击激活、input/change 事件。 |
| `input type=radio` | 子集 | checked 状态和绘制；同 name 互斥组仍有限。 |
| `input type=range` | 可用 | track/thumb 绘制，拖动更新 value。 |
| `select` / `option` / `optgroup` | 子集 | 绘制当前选中项；验证壳点击会循环选项；Up/Down 可跨 `optgroup` 在 option 间移动。无 popup/分组菜单 UI。 |
| `progress` / `meter` | 可用 | 根据属性绘制 value bar。 |
| 日期/颜色/文件控件 | 延后 | 暂用 text/select/range fallback。 |
| 表单验证 | 延后 | 无 constraint validation UI 或 form submit。 |
| IME | 壳层相关 | 核心接收 UTF-8 文本；平台壳负责输入法集成。 |

## 事件与输入

| 功能 | 状态 | 行为 |
| --- | --- | --- |
| `EventTarget` | 可用 | 按类型紧凑存储 listener。 |
| 捕获/目标/冒泡 | 可用 | 类 DOM 事件流。 |
| `preventDefault` | 可用 | event object 记录取消状态。 |
| `stopPropagation` / `stopImmediatePropagation` | 可用 | 已实现。 |
| `MouseEvent` | 可用 | `clientX`、`clientY`、`button`、`buttons`、modifier 字段。 |
| `WheelEvent` | 可用 | `deltaX`、`deltaY`、`deltaMode`、modifier 字段。 |
| Hit testing | 可用 | 基于 layer/layout geometry，考虑裁剪和 z-order hint。 |
| Pointer move/down/up | 可用 | 平台无关 input controller 派发 mouse-like events，并提供 `pointerdown`/`pointerup` aliases。 |
| Click synthesis | 可用 | 同一目标 down/up 合成 click。 |
| Hash anchor click | 壳层限定 | Win32 壳会把 `<a href="#id">` 处理为 viewport scroll。核心只派发 click 事件。 |
| Focus tracking | 子集 | input controller 保存 focused node，并驱动 `:focus` / `:focus-within` 样式匹配。 |
| Touch events | 子集 | `touchstart`/`touchend` 以 mouse-like event 暴露，用于按下反馈；完整 multi-touch object 延后。 |
| Keyboard events | 延后 | 核心只处理控件所需的简单 key action；DOM keyboard event object 不完整。 |

## JavaScript / JerryScript 绑定

JavaScript 只在 `JELLYFRAME_BUILD_SCRIPTING=ON` 且通过 `JERRYSCRIPT_ROOT` 配置本地
JerryScript 源码树时可用。

| API | 状态 | 行为 |
| --- | --- | --- |
| Classic document scripts | 子集 | scripting 构建中，伪浏览器/Win32 壳会执行 inline classic `<script>`，并通过宿主 callback 加载本地外部 `<script src>`。 |
| `window` / `document` | 子集 | 暴露下列方法。 |
| `document.getElementById` | 可用 | 返回 wrapper 或 `null`。 |
| `document.createElement` | 可用 | 创建由 runtime 持有、等待挂载的 detached element；数量受 `HostBudgets::max_detached_dom_nodes` 限制。 |
| `document.createTextNode` | 可用 | 创建 detached text node，同样受 detached-node 预算限制。 |
| `appendChild` / `removeChild` | 可用 | 移动节点、防止环、标记 dirty。`removeChild` 返回的节点会继续由 runtime 持有，保持可用。 |
| `setAttribute` / `getAttribute` / `removeAttribute` | 可用 | 绑定层会 lowercase 属性名。 |
| `textContent` | 可用 | getter/setter；同值设置不会触发 dirty。 |
| `children` / `parentElement` | 子集 | element children 快照数组，以及 parent wrapper/null。 |
| `matches` / `closest` | 子集 | 简单 tag、`.class`、`#id`、`[attr]` 和 `[attr=value]` selector；不支持 combinator。 |
| `dataset` | 子集 | 已存在的 `data-*` 属性以 camelCase 快照 property 暴露，用于事件委托；动态新 key 延后。 |
| `element.style` | 子集 | 可写 inline style object，支持 `display`、`color`、`background`、`backgroundColor`、`textAlign`、`fontWeight`、`width`、`height`。 |
| `hidden` / `disabled` properties | 子集 | Boolean reflection。`hidden` 会移出渲染；disabled 表单控件不会激活或接收文本输入。 |
| `addEventListener` / `removeEventListener` | 可用 | JS callback 桥接到核心事件派发。 |
| Event object | 子集 | `type`、`target`、`currentTarget`、phase、取消/停止传播 API、鼠标/滚轮字段。 |
| 表单属性 | 子集 | 相关控件上的 `value`、`checked`、`selectedIndex`。 |
| Timer | 可用 | 宿主泵动 `setTimeout`、`clearTimeout`、`setInterval`、`clearInterval`；callback budget 由宿主控制。 |
| Promise/microtask | 延后 | 不要依赖浏览器 task 语义。 |
| Modules/import | 延后 | `type="module"`、dynamic import 和 module loading 会跳过。 |
| `querySelector` | 延后 | 现阶段使用 ID。 |
| `innerHTML` | 延后 | 使用 DOM creation APIs。 |
| XHR/fetch/storage | 部分支持 | scripting 构建已支持异步 `XMLHttpRequest` GET V0；绑定非阻塞 `AppLocalStorageShadow` 时支持极小 `localStorage` 子集；`fetch()` 等 Promise/microtask 有界后再考虑。 |
| 文本检索式兼容性检查 | 已弃用 | 旧的 HTML/CSS/JS substring 扫描不再用于兼容性判断。未来 diagnostics 必须来自实际 parse、style、layout、render 或 load 该功能的管线组件。 |
| 管线 diagnostics | 已开始 | HTML tokenizer/parser、CSS parser、style resolver、render tree、layout、layer tree、script collection、package/resource loader 和 software renderer 会通过可选 sink 向桌面工具报告预算截断、跳过、忽略、加载失败和降级。`jellyframe_pseudo_browser --diagnostics-json` 会输出结构化报告，`jellyframe_cli.py check`/`preview`/`package` 会把它合并到 `pipelineDiagnostics`。原则是：已知不兼容给出明确原因；未知或无法分类的异常至少给出触发字段或片段。 |
| 字体资源检查 | 工具限定 | `jellyframe_font_resource_check` 暂时保留用于确定性的字体工作：输出非 ASCII 使用字符、估算 bitmap font 预算，并验证嵌入式字体覆盖。 |

## 渲染与像素输出

| 功能 | 状态 | 行为 |
| --- | --- | --- |
| Display list | 可用 | 矩形、边框、渐变、文本命令，包含近似文本字重。 |
| CPU framebuffer | 可用 | 软件 rasterizer/compositor 可输出 BMP/PPM。带预算的 compositor 会在分配前拒绝过大的主 framebuffer。 |
| 嵌入式 framebuffer adapter | 可用 | `embedded_framebuffer` 可把 `HostFrameBufferView` 转换到调用方持有的 RGBA8888/BGRA8888、RGB565/BGR565、RGB332、Gray8 或 1-bit 单色 buffer，并通过 callback flush dirty rects。 |
| Source-over alpha | 可用 | straight-alpha 合成。 |
| Opacity layer | 子集 | opacity/composited layer 使用离屏合成。嵌入式宿主可限制 offscreen pixels；超限 layer 会降级为逐命令直接透明绘制，避免分配过大的临时 buffer。 |
| 圆角填充 | 子集 | 背景/阴影支持 rounded rectangle fill clipping。 |
| 边框绘制 | 可用 | 边框拆成 fill rectangles。 |
| Linear gradient | 子集 | 两色水平/垂直渐变命令。 |
| 文本 | 子集 | 核心 fallback 是极小 ASCII bitmap 绘制，并为 UTF-8 非 ASCII 码点显示占位 glyph。Win32 壳注入 GDI，可验证 UTF-8/中文。 |
| 中文文本 | 壳层相关 | 用 Win32 壳或未来平台 text backend。伪浏览器 fallback 会显示占位 glyph。 |
| 图片 | 宿主可选/调试可用 | 已有平台无关 `ImageDecodeMock`、`AppImageSurfaceCache`、`Surface` handle 生命周期和尺寸/decoded bytes/pending 预算检查。render core 支持 `ImageHandleResolver` + image display command + `ImagePainter`；Win32 debug 壳可让 `<img src="app://icon">` / `app://photo` 自动提交 mock decode 并重绘，并可从 `.jfapp`/源码包加载无压缩 24/32-bit BMP 作为包内图片 V0。`AppImageSurfaceCache` 可按 ready surface 数量和 decoded bytes 回收未被当前 display list 引用的 LRU surface；图片命令携带 `object-fit` 子集；图片 request 拒绝和 completion 失败会归类为稳定原因，例如 `capability-denied`、`resource-not-found`、`decode-budget-exceeded`、`surface-budget-exceeded`。PNG/JPEG/WebP、复杂 `object-position` 和产品级 MCU codec 仍待接入。 |
| 音频播放 | 宿主可选/runtime mock | 核心不处理 PCM/I2S/codec。`app_runtime` 已有 `AudioCommandMock`，覆盖 open/play/pause/stop/close/setVolume、`AudioStream` handle 生命周期、ended/error completion 和 stream 预算检查。manifest 可声明 `media.audio.mp3`，但面向用户的 JS 音频 API 和真实 MCU MP3 pipeline 仍属于宿主/后续工作。 |
| 轻量视频/MJPEG/H.264 | 实验/宿主可选 | 可作为低分辨率 frame provider 规划；不承诺 `<video>`。ESP32-S3 H.264 复测已跑通但低于实时，只能放在显式实验 profile，不在默认 profile。 |
| Canvas/SVG | 延后 | 无 canvas API 或 SVG renderer。 |
| 真实 shadow blur | 延后 | `box-shadow` blur 只近似。 |
| Filter/blend modes | 延后 | 仅 normal source-over。 |
| GPU compositing | 延后 | 当前 CPU-only；layer model 为未来硬件后端留接口。 |

## Dirty 与重绘

| 机制 | 状态 | 行为 |
| --- | --- | --- |
| Dirty propagation | 可用 | mutation 会把 dirty bits OR 到当前节点和祖先。 |
| Dirty check | 可用 | 因为祖先保存聚合 dirty bits，根节点检查为 O(1)。 |
| Dirty clear | 可用 | 清理时跳过干净分支。 |
| 宿主合并重绘 | 子集 | Win32 壳会在 input/script callback 后只对 dirty DOM 重绘。 |
| 增量 style/layout | 子集 | paint-only 表单控件状态变化可在 Win32 验证壳中复用 render/layout，只重建 layer/display commands。文本、style 和树结构变化仍重建 render/layout。 |
| Dirty rectangle repaint | 子集 | `dirty_region` 会通过对比旧/新 layout box，或对 paint-only 变化复用同一份 layout，为直接文本、属性、表单控件绘制变化计算有界重绘区域。树结构变化保守重绘 viewport。若估算 dirty area 过大，宿主也可以选择全帧重绘，避免局部 flush 反而更贵。 |
| Animation invalidation | 子集 | `animation_invalidation` 可根据上一帧/当前帧的 animation style overrides，在当前 layout tree 上生成局部 dirty rectangles，覆盖 opacity/color paint-only 动画和 translate/scale transform 前后位置。 |
| Display invalidation 诊断 | 可用 | `analyze_display_invalidation(...)` 会报告 dirty rectangles 对 layer 和 display command 的覆盖情况。它只提供诊断；retained display-list reuse 仍延后。 |
| Host frame sink | 子集 | `present_frame` 可以通过 `HostFrameSink` 暴露 `FrameBuffer`，并携带可选 dirty rects。成功 `present` 是 frame buffer 可复用边界：异步 panel/DMA 宿主必须等待、复制到 driver-owned 内存，或阻止下一帧 render 直到 flush 完成。`embedded_framebuffer` 已提供可移植像素转换；真实显示 I/O 仍由宿主负责。 |
| Host device capabilities | 草案 | `HostDeviceCapabilities` 记录开发板 port 的显示、输入、内存、budget 和服务 flags。当前核心把它作为契约/策略输入文档；更深的自动适配仍延后。 |
| Host budgets | 子集 | `HostBudgets` 已接入 HTML/CSS parsing、render/layout/layer tree 上限、display-list 上限、dirty rect 数量、frame-loop input/timer/animation 上限、JerryScript timer/rAF/listener 限制、detached DOM node 限制和软件 compositor 的主/offscreen pixel 上限。Render/layout/layer tree 已有 arena-backed 构建路径；完整 mutable DOM arena 仍是后续工作。 |
| Frame scratch | 可用 | `FrameScratch` 复用 dirty-region bounds、dirty rectangles 和 animation overrides；`AppFrameScratch` 复用 host completion batch/accepted list。常规帧清空复用，息屏、切 app 或内存压力时显式 `release()`。真实 DMA/panel buffer 仍由 port 管理。 |

实际含义：脚本中多次 DOM mutation 应尽量放在同一次事件或 timer callback 内。宿主会观察到一次
dirty 并重绘一次。paint-only 表单控件变化在 Win32 验证壳中可以避免 render/layout 重建；其他变化仍会走简化管线
pass，但 framebuffer 阶段已经能对非结构性变化做有界 dirty rectangle 局部重绘。

## 推荐写法

- 交互节点优先使用稳定 `id`。
- 使用简单 selector 和 class-based styling。
- 仪表盘/卡片 UI 使用已支持的 grid-card 子集。
- 使用 `button`、`input`、`select`、`textarea`、`progress`、`meter`，不要手写 canvas 控件。
- 对现代 CSS 值写 fallback，例如：`color: #334155; color: oklch(...);`。
- 不要把必需 UI 放进 `@container`、不支持或复杂的 `@supports`、不支持/复杂 media query 或 `:has()` 等不支持 selector 函数里。
- 脚本保持同步、小型；数据由宿主提供。
- 中文/UTF-8 可读性验收使用 Win32 壳，因为它同时通过文本后端 API 注入测量和绘制。
- 页面保持小而有界；parser limit 是设计目标，不是 bug。

## 当前硬限制

- CSS parser：`max_rules` 4096，`max_declarations_per_rule` 256，
  `max_nesting_depth` 8，默认 media viewport 为 360x240。
- 默认 host budgets 会限制 DOM nodes、render objects、layout boxes、layers、
  display commands、dirty rects、timers、listeners 和 framebuffer pixels。
- 诊断/示例工具会限制输入文件，通常为 512 KiB 或 1 MiB，取决于工具。
- Grid 自动列数内部有界，以保证嵌入式内存可预测。
- 当前 JerryScript 构建假设同一进程内只有一个 active runtime。

## 什么时候应该新增功能

适合新增：

- 嵌入式应用作者常用。
- 可用整数或有界算法计算。
- 能一致降级。
- 不要求网络、GPU、字体加载或大型浏览器服务。

应延后：

- 会引入 style/layout 反馈循环，例如 `@container`。
- 需要大型外部子系统，例如 image decode、font shaping、canvas。
- 依赖完整浏览器 task/loading 模型。
- 会造成“半现代半老旧”的不一致视觉结果。
