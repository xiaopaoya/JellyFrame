# App 作者能力速查表

> 最后更新：2026-07-22；适用版本：0.5.0-dev

这份表给 app 作者快速判断“能不能用”。完整边界仍以
[developer_capability_matrix_zh.md](developer_capability_matrix_zh.md) 为准。

## HTML

| 能力 | 状态 | 建议 |
| --- | --- | --- |
| 普通结构标签 | 可用 | 使用 `div`、`section`、`header`、`main`、`footer`、`ul`、`li`、`p`、`span`。未知标签会当普通元素处理。 |
| 文本 | 可用 | UTF-8 会保留；真实中文字形取决于系统字体或 app `.jffont` 补充含。 |
| 表单控件 | Form V0 子集 | 可用 `button`、`input`、`select`、`textarea`、`progress`、`meter`。`required`、文本 `minlength`/`maxlength`、checkbox/radio 必填状态和 required select 可配合本地 `submit` 使用；不提供浏览器校验弹窗或页面导航。 |
| 确认对话框 | 有界 modal 子集 | 使用一个 `<dialog>` 配合 `showModal()` 和 `close(returnValue)`。Win32 壳会把 Escape 转为可取消的 `cancel`，再触发 `close`；打开期间 focus/hit test 限制在 dialog 内。不支持嵌套 dialog、backdrop/light-dismiss、`show()`、`requestClose()` 或浏览器 top layer。 |
| 图片 | 子集 | 使用 package-local `<img src="/assets/icon.bmp">` 或一个 CSS `background-image: url("/assets/image.bmp")`。静态图标 SVG 可通过 `package --rasterize-svg` 在打包期编译成 BMP，生成包中的 HTML/CSS 引用会自动改写。`border-radius` 会以抗锯齿角裁切这两类图片。CSS 背景会填充元素并保留 `background-color` fallback；不支持远程/data URL、repeat、position、size。Win32 验收内置 BMP；PNG/JPEG/WebP 需要 target/host codec adapter。 |
| 浏览器图片/媒体标签 | 延后 | 不依赖 `picture`/`source`/`srcset`、`<video>`、`<track>` 或 `<audio>` markup。由 app state 选择一张包内图片；只有声明对应 capability 时才使用 `Audio()` V0 或 host frame-provider contract。 |
| 表格、ruby 与 template | 延后 | 没有浏览器表格测量、ruby/bidi layout 或分离的 `template.content` 语义。使用文档化 flex/grid 子集、本地化 plain text 和显式 DOM 创建。 |
| 富文本编辑 | 延后 | 不提供 `contenteditable`、Selection、Range。使用有界 `input`/`textarea`，或产品自有的系统编辑器。 |
| Canvas | 可选 | 需要 manifest `graphics.canvas2d`，并确认 target 支持。适合小图表/仪表，不适合重写整个 UI。 |
| 远程页面资源 | 不支持 | 不要远程加载 HTML/CSS/JS/image/font。运行时数据请求走 XHR host service。 |

## CSS

写样式前如需查询完整 CSSWG 特性支持性，请读
[csswg_support_table_zh.md](csswg_support_table_zh.md)。它和 HTML 支持表使用同一组状态；
`partial` 表示已文档化的属性/值子集，不代表完整浏览器语义。

| 能力 | 状态 | 建议 |
| --- | --- | --- |
| 基础颜色/背景 | 可用子集 | 使用 hex、命名色、`rgb()` / `rgba()` 和有界 sRGB `hsl()` / `hsla()`。包内图片背景使用 `background-color` 加 `background-image: url("/assets/image.bmp")`，再按需使用 `background-size: cover`/`contain`/`100% 100%`、简单 `background-position` 子集和 `background-repeat: no-repeat`；package report 会指出非法或缺失路径。 |
| Layout | 子集 | 优先 block、简化 flex（direct flex child 可用有符号整数 `order`）和有界 grid-card 子集。Grid 支持 2-4 条固定/`1fr` 行，以及正整数 `grid-column`/`grid-row` start/end/span placement；它不是完整 Grid。使用固定底栏和明确滚动容器。需要保留 layout 占位时使用 `visibility: hidden`；需要收缩占位时使用 `display: none`。 |
| 响应式 | 子集 | 使用 `@media`、百分比 sizing、LTR `inline-size` / `block-size`、`max-width: 100%`、`box-sizing: border-box`、`gap`、`aspect-ratio`。固定高度纵向滚动容器使用文档化的 `overflow-y: auto` 子集。 |
| 圆角/阴影/渐变 | 子集 | 支持圆角矩形、百分比圆角、轻量阴影、非布局 `outline-offset`、线性渐变、两段 `conic-gradient()` 进度环和两色中心圆形 `radial-gradient()` 高光。复杂 blur/mask/filter 延后。 |
| 文本排版与溢出 | 有界子集 | 短标签/数字可使用 `letter-spacing`；无法断开的 UTF-8 标签可使用 `overflow-wrap: anywhere`，它只在 scalar 边界断行。`text-wrap: wrap` 和 `text-wrap: nowrap` 分别是 `white-space: normal` 和 `white-space: nowrap` 的别名；超宽单行文本使用 `white-space: nowrap; text-overflow: ellipsis`，会绘制 UTF-8 安全前缀与 `...`。不提供 hyphenation、平衡换行或复杂文字 shaping。 |
| 动画 | 子集 | 关键帧支持 opacity、color、background/background-color 与已文档化的 transform 形式；timing 支持 linear/ease/ease-in/ease-out/ease-in-out，打包前会标记 layout 动画与不支持的 easing。 |
| CSS nesting | 显式单层子集 | 只使用显式 `&`，例如 `.card { &:hover { ... } & .label { ... } }`。不要使用隐式 nesting、nested at-rule 或超过一层的嵌套。 |
| 复杂浏览器 CSS | 不支持/延后 | 不依赖完整 grid/flex、container query、`:has()`、完整 pseudo-elements、filter/backdrop-filter。 |

## JavaScript

| 能力 | 状态 | Manifest |
| --- | --- | --- |
| DOM 查询/修改 | 子集 | 无需额外 capability。使用 `document.head`、`document.body`、`document.readyState`、`document.defaultView`、`document.hasFocus()`、简单 selector 的 `querySelector`、`createElement`、`appendChild`、`append`、`prepend`、`textContent`、轻量 `innerText`、`id`、`className`、常用表单控件 IDL 属性和小型 `classList` helper。 |
| 元素几何 | frame snapshot 子集 | `element.getBoundingClientRect()` 返回上一个完成的宿主 layout frame 的只读数值 client-relative 矩形。它不强制 layout、不保留 live DOMRect，也不包含 transform/nested-scroll 后的几何。 |
| 事件 | 可用 | 使用 `addEventListener`、文档化的 `on*` handler property、`element.click()`、事件委托、可读写的有界 `dataset` 子集和 `matches`/`closest`。不支持 HTML inline event attribute。 |
| 本地表单 / `FormData` | Form V0 子集 | 使用 `form.checkValidity()`、`reportValidity()`、`requestSubmit([submitter])`、可取消的 `submit` 事件及 `event.submitter`，以及 `new FormData(form)`。字符串 entry 支持 `append`、`set`、`delete`、`get`、`getAll`、`has` 和 `forEach`；后者在有界快照上运行，不是 live iterator。在事件处理器中通过已获准的宿主服务提交数据。 |
| `HTMLDialogElement` | 有界 modal 子集 | scripting 构建支持 `dialog.open`、`returnValue`、`showModal()` 和 `close([returnValue])`。使用 `cancel`/`close` listener；每个 document 同时只能有一个 modal，宿主 back/Escape policy 可请求取消。 |
| 时间 / Timer / rAF | 有界 | `Date.now()` 读取宿主注入时间；timer/rAF 无需额外 capability，但受 frame policy 和预算限制。 |
| `XMLHttpRequest` GET | 宿主可选 | `network.fetch`。只用于运行时数据，不加载页面资源。 |
| `localStorage` | 宿主可选 | `storage.kv`。这是 app 私有小型 KV shadow，不是浏览器持久存储全集。 |
| 浏览器会话与消息通道 | 延后 | 不提供 `sessionStorage`、cookie、storage event、MessageChannel/MessagePort。状态留在当前 app，或使用明确的 host service。 |
| `Audio()` | 宿主可选 | `media.audio.playback`。真实 codec/I2S 由 host 提供。 |
| `navigator.geolocation` | 宿主可选 | `location.position`。只提供离散定位快照。 |
| Canvas 2D | 宿主可选子集 | `graphics.canvas2d`。只在 `getContext("2d")` 后分配 backing store。包含 canvas-to-canvas `drawImage()` 的 3/5/9 参数形式和最近邻缩放、双 stop 同心 `createRadialGradient()`，以及由 `save()`/`restore()` 保存的像素对齐 `translate(x, y)` / `resetTransform()`，和有界 `quadraticCurveTo()` / `bezierCurveTo()` path tessellation。`<img>`、`ImageBitmap`、视频 source、焦点/非同心或多 stop 径向渐变、scale、rotate、通用矩阵与像素 API 仍延后。 |
| 宿主时间 | 可用 | 使用 `Date.now()`。除非后续明确文档化，不要假设 `new Date()` 已受宿主时钟控制。 |
| 宿主计算任务 | 宿主可选合同 | `compute.jobs` 预留有字节预算的具名宿主任务；它暂时不是 JS Worker、线程、消息端口或任意代码执行 API。 |
| 视频帧预览 | 宿主可选实验合同 | `media.video.frame` 为产品宿主的 MJPEG 或显式启用的 H.264 baseline 预览提供有界最新帧句柄。它不是 `<video>` 或 JS 媒体 API。 |
| 电池/天气/活动摘要 | 宿主可选 | 声明 `system.battery`、`system.weather` 或 `system.activity`，再用 `navigator.jellyframe.getSnapshot()` 读取已批准的低频快照；不提供轮询、订阅或裸设备句柄。 |
| App 内路由 | 有界 | `location.hash`、`hashchange`、`popstate`、`onhashchange` 和 `onpopstate` 只切换当前 app 内部状态。有界 `history.length`、`back()`、`forward()`、`go()`、`pushState()`、`replaceState()` 只保存 fragment entry；URL 加载、浏览器 history state、导航和跨 app 路由均不存在。 |
| 静态本地模块 | 打包期子集 | 一个外部 `type="module"` 入口及包内静态 `.js` import 会在打包期合成为 classic script。动态 `import()`、远程模块与 `modulepreload` 延后。 |
| Promise/fetch/innerHTML | 延后 | 不要依赖。 |
| querySelector/querySelectorAll | 子集 | 仅支持简单 tag、`.class`、`#id`、`[attr]`、`[attr=value]` 和同一 compound 组合；复杂 selector 会诊断。 |

## 资源和字体

| 能力 | 状态 | 建议 |
| --- | --- | --- |
| Package-local 资源 | 可用 | 使用 `/assets/a.bmp` 或相对路径。禁止 scheme、`//host`、逃出 app root。静态 SVG 图标源仅会在传入 `--rasterize-svg` 时作为打包期输入，不会以 SVG 形式进入目标端。 |
| `.jfapp` 安装包 | 可用 V0 | 用 CLI `package --output-bundle` 生成。 |
| 字符扫描 | 默认 | `check`/`package`/`preview`/`install` 默认输出 `*.used_chars.txt` 和 `fontSubset` 计划。 |
| `.jffont` | 可用 | 可由授权 BDF 生成；生成后仍要显式写入 manifest `fonts[]`。 |
| 字体抗锯齿 | 可选 | `.jffont` V1 支持 2bpp/4bpp coverage，换取更多字体字节和绘制成本。 |

## 最小发布检查

```powershell
python tools\jellyframe_cli.py check `
  --root your_app `
  --target round-300 `
  --targets round-300,rect-320x240,rect-172x320 `
  --report build\your_app.report.json `
  --build-dir build\Release
```

优先看 report 中的 `developerAdvice[]`。它会告诉你哪些能力没声明、哪个 target 溢出、哪些字体或资源需要处理。
