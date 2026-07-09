# App 作者能力速查表

> 最后更新：2026-07-10；适用版本：0.5.0-dev

这份表给 app 作者快速判断“能不能用”。完整边界仍以
[developer_capability_matrix_zh.md](developer_capability_matrix_zh.md) 为准。

## HTML

| 能力 | 状态 | 建议 |
| --- | --- | --- |
| 普通结构标签 | 可用 | 使用 `div`、`section`、`header`、`main`、`footer`、`ul`、`li`、`p`、`span`。未知标签会当普通元素处理。 |
| 文本 | 可用 | UTF-8 会保留；真实中文字形取决于系统字体或 app `.jffont` 补充含。 |
| 表单控件 | Form V0 子集 | 可用 `button`、`input`、`select`、`textarea`、`progress`、`meter`。`required`、文本 `minlength`/`maxlength`、checkbox/radio 必填状态和 required select 可配合本地 `submit` 使用；不提供浏览器校验弹窗或页面导航。 |
| 图片 | 子集 | 使用 package-local `<img src="/assets/icon.bmp">`。Win32 验收内置 BMP；PNG/JPEG/WebP 需要 target/host codec adapter。 |
| Canvas | 可选 | 需要 manifest `graphics.canvas2d`，并确认 target 支持。适合小图表/仪表，不适合重写整个 UI。 |
| 远程页面资源 | 不支持 | 不要远程加载 HTML/CSS/JS/image/font。运行时数据请求走 XHR host service。 |

## CSS

| 能力 | 状态 | 建议 |
| --- | --- | --- |
| 基础颜色/背景 | 可用 | 使用 hex、命名色、`rgb()`、`rgba()`。 |
| Layout | 子集 | 优先 block、简化 flex、grid-card 子集、固定底栏和明确滚动容器。 |
| 响应式 | 子集 | 使用 `@media`、百分比 sizing、`max-width: 100%`、`box-sizing: border-box`、`gap`、`aspect-ratio`。 |
| 圆角/阴影/渐变 | 子集 | 支持圆角矩形、百分比圆角、轻量阴影、线性渐变、两段 `conic-gradient()` 进度环和两色中心圆形 `radial-gradient()` 高光。复杂 blur/mask/filter 延后。 |
| 文本溢出 | 诊断子集 | `white-space: nowrap` / `text-overflow` 可表达意图；实际溢出会进 report。 |
| 动画 | 子集 | 优先 opacity、color、background-color、translate/scale/rotate。避免 layout 属性动画。 |
| 复杂浏览器 CSS | 不支持/延后 | 不依赖完整 grid/flex、container query、`:has()`、完整 pseudo-elements、filter/backdrop-filter。 |

## JavaScript

| 能力 | 状态 | Manifest |
| --- | --- | --- |
| DOM 查询/修改 | 子集 | 无需额外 capability。使用 `document.head`、`document.body`、`document.readyState`、`document.defaultView`、`document.hasFocus()`、简单 selector 的 `querySelector`、`createElement`、`appendChild`、`textContent`、轻量 `innerText`、`id`、`className`、常用表单控件 IDL 属性和小型 `classList` helper。 |
| 事件 | 可用 | 使用 `addEventListener`、文档化的 `on*` handler property、`element.click()`、事件委托、`dataset`、`matches`/`closest` 子集。不支持 HTML inline event attribute。 |
| 本地表单 / `FormData` | Form V0 子集 | 使用 `form.checkValidity()`、`reportValidity()`、`requestSubmit([submitter])`、可取消的 `submit` 事件及 `event.submitter`，以及 `new FormData(form)`。在事件处理器中通过已获准的宿主服务提交数据。 |
| 时间 / Timer / rAF | 有界 | `Date.now()` 读取宿主注入时间；timer/rAF 无需额外 capability，但受 frame policy 和预算限制。 |
| `XMLHttpRequest` GET | 宿主可选 | `network.fetch`。只用于运行时数据，不加载页面资源。 |
| `localStorage` | 宿主可选 | `storage.kv`。这是 app 私有小型 KV shadow，不是浏览器持久存储全集。 |
| `Audio()` | 宿主可选 | `media.audio.playback`。真实 codec/I2S 由 host 提供。 |
| `navigator.geolocation` | 宿主可选 | `location.position`。只提供离散定位快照。 |
| Canvas 2D | 宿主可选子集 | `graphics.canvas2d`。只在 `getContext("2d")` 后分配 backing store。包含 canvas-to-canvas `drawImage()` 的 3/5/9 参数形式和最近邻缩放、双 stop 同心 `createRadialGradient()`，以及由 `save()`/`restore()` 保存的像素对齐 `translate(x, y)`，和有界 `quadraticCurveTo()` / `bezierCurveTo()` path tessellation。`<img>`、`ImageBitmap`、视频 source、焦点/非同心或多 stop 径向渐变、scale、rotate、通用矩阵与像素 API 仍延后。 |
| 宿主时间 | 可用 | 使用 `Date.now()`。除非后续明确文档化，不要假设 `new Date()` 已受宿主时钟控制。 |
| 宿主计算任务 | 宿主可选合同 | `compute.jobs` 预留有字节预算的具名宿主任务；它暂时不是 JS Worker、线程、消息端口或任意代码执行 API。 |
| 视频帧预览 | 宿主可选实验合同 | `media.video.frame` 为产品宿主的 MJPEG 或显式启用的 H.264 baseline 预览提供有界最新帧句柄。它不是 `<video>` 或 JS 媒体 API。 |
| 天气/活动/电量 | 宿主/system only | 天气 app 数据应使用 XHR JSON；活动和电量 summary 目前不是普通 app JS API。 |
| App 内路由 | 有界 | `location.hash`、`hashchange` 和 `onhashchange` 只切换当前 app 内部状态。URL 加载、`history`、导航和跨 app 路由均不存在。 |
| 静态本地模块 | 打包期子集 | 一个外部 `type="module"` 入口及包内静态 `.js` import 会在打包期合成为 classic script。动态 `import()`、远程模块与 `modulepreload` 延后。 |
| Promise/fetch/innerHTML | 延后 | 不要依赖。 |
| querySelector/querySelectorAll | 子集 | 仅支持简单 tag、`.class`、`#id`、`[attr]`、`[attr=value]` 和同一 compound 组合；复杂 selector 会诊断。 |

## 资源和字体

| 能力 | 状态 | 建议 |
| --- | --- | --- |
| Package-local 资源 | 可用 | 使用 `/assets/a.bmp` 或相对路径。禁止 scheme、`//host`、逃出 app root。 |
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
