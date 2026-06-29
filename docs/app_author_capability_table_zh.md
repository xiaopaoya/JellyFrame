# App 作者能力速查表

这份表给 app 作者快速判断“能不能用”。完整边界仍以
[developer_capability_matrix_zh.md](developer_capability_matrix_zh.md) 为准。

## HTML

| 能力 | 状态 | 建议 |
| --- | --- | --- |
| 普通结构标签 | 可用 | 使用 `div`、`section`、`header`、`main`、`footer`、`ul`、`li`、`p`、`span`。未知标签会当普通元素处理。 |
| 文本 | 可用 | UTF-8 会保留；真实中文字形取决于系统字体或 app `.jffont` 补充包。 |
| 表单控件 | 子集 | 可用 `button`、`input`、`select`、`textarea`、`progress`、`meter`。复杂浏览器默认 UI 不保证一致。 |
| 图片 | 子集 | 使用 package-local `<img src="/assets/icon.bmp">`。Win32 验收内置 BMP；PNG/JPEG/WebP 需要 target/host codec adapter。 |
| Canvas | 可选 | 需要 manifest `graphics.canvas2d`，并确认 target 支持。适合小图表/仪表，不适合重写整个 UI。 |
| 远程页面资源 | 不支持 | 不要远程加载 HTML/CSS/JS/image/font。运行时数据请求走 XHR host service。 |

## CSS

| 能力 | 状态 | 建议 |
| --- | --- | --- |
| 基础颜色/背景 | 可用 | 使用 hex、命名色、`rgb()`、`rgba()`。 |
| Layout | 子集 | 优先 block、简化 flex、grid-card 子集、固定底栏和明确滚动容器。 |
| 响应式 | 子集 | 使用 `@media`、百分比 sizing、`max-width: 100%`、`box-sizing: border-box`、`gap`、`aspect-ratio`。 |
| 圆角/阴影/渐变 | 子集 | 支持圆角矩形、百分比圆角、轻量阴影、线性渐变和两段 `conic-gradient()` 进度环。复杂 blur/mask/filter 延后。 |
| 文本溢出 | 诊断子集 | `white-space: nowrap` / `text-overflow` 可表达意图；实际溢出会进 report。 |
| 动画 | 子集 | 优先 opacity、color、background-color、translate/scale/rotate。避免 layout 属性动画。 |
| 复杂浏览器 CSS | 不支持/延后 | 不依赖完整 grid/flex、container query、`:has()`、完整 pseudo-elements、filter/backdrop-filter。 |

## JavaScript

| 能力 | 状态 | Manifest |
| --- | --- | --- |
| DOM 查询/修改 | 子集 | 无需额外 capability。使用 `getElementById`、`createElement`、`appendChild`、`textContent`、`className`。 |
| 事件 | 可用 | 使用 `addEventListener`、事件委托、`dataset`、`matches`/`closest` 子集。 |
| Timer / rAF | 有界 | 无需额外 capability，但受 frame policy 和预算限制。 |
| `XMLHttpRequest` GET | 宿主可选 | `network.fetch`。只用于运行时数据，不加载页面资源。 |
| `localStorage` | 宿主可选 | `storage.kv`。这是 app 私有小型 KV shadow，不是浏览器持久存储全集。 |
| `Audio()` | 宿主可选 | `media.audio.mp3`。真实 codec/I2S 由 host 提供。 |
| `navigator.geolocation` | 宿主可选 | `location.position`。只提供离散定位快照。 |
| Canvas 2D | 宿主可选 | `graphics.canvas2d`。只在 `getContext("2d")` 后分配 backing store。 |
| Promise/fetch/modules/querySelector/innerHTML | 延后 | 不要依赖。 |

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
