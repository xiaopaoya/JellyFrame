# 脚本能力范围

JellyFrame 的脚本能力保持小型、可选、有界。目标是让嵌入式 app UI 真正可用，而不是一次性继承完整浏览器
API 表面。

## Runtime Shell

- 由 `JELLYFRAME_BUILD_SCRIPTING=ON` 控制的可选 `jellyframe_script` target。
- `JERRYSCRIPT_ROOT` 可以指向官方 JerryScript checkout，例如 `third_party/jerryscript`。
- `JerryScriptRuntime` 管理 JerryScript 生命周期。
- `eval(source, source_name)` 执行 classic JavaScript 源码。
- 返回字符串化后的成功结果，或字符串化后的异常结果。
- 同一进程内可重复初始化/清理；当前实现同一时间只允许一个 runtime 存活。
- `jellyframe_pseudo_browser --script file.js` 用于桌面验收。

## DOM Binding

- 宿主绑定 native DOM tree 后，提供全局 `window` 和 `document` 对象。
- `document.getElementById(id)`。
- `document.createElement(tag)`。
- `document.createTextNode(text)`。
- `node.appendChild(child)`，并处理 detached node 的所有权转移。
- `node.removeChild(child)`，返回的 wrapper 仍可继续使用。
- detached node 在未挂载到 document 前由 runtime 通过 `DomOwner` 持有。
  `HostBudgets::max_detached_dom_nodes` 会限制脚本创建和移除后保留的节点数量，
  避免嵌入式 app 无界扩大该池。
- `element.setAttribute(name, value)`。
- `element.getAttribute(name)`。
- `node.textContent` getter/setter。
- `jellyframe_pseudo_browser --script file.js` 会在执行脚本前绑定解析后的页面 DOM，
  因此脚本 mutation 会影响最终渲染输出。

## Events

- `node.addEventListener(type, callback, options)`。
- `node.removeEventListener(type, callback)`。
- listener options：boolean capture，以及对象形式 `{ capture, once }`。
- Event object 字段：`type`、`target`、`currentTarget`、`eventPhase`、`bubbles`、
  `cancelable`、`defaultPrevented`，以及适用时的鼠标坐标/按键、modifier keys 和滚轮 delta。
- Event object 方法：`preventDefault`、`stopPropagation` 和 `stopImmediatePropagation`。
- JavaScript listener 复用现有 C++ capture/target/bubble 事件流，并可在 native input dispatch
  过程中修改 DOM。
- scripting 构建中的 Win32 browser shell 支持 `--script file.js`，并在脚本事件回调弄脏 DOM 后重绘。

## Form Controls

- 相关表单控件节点上的属性：
  - `input.value`
  - `textarea.value`
  - `checkbox.checked`
  - `radio.checked`
  - `select.value`
  - `select.selectedIndex`
- 原生文本输入、Backspace、checkbox/radio/select 激活和 range 拖动会更新 JavaScript 可见的控件状态。
- 原生输入派发会通过现有 C++ 事件流触发 JavaScript 可观察的 `input` 和 `change` 事件。
- JavaScript 修改表单状态后会标记 DOM dirty，宿主可以据此重绘轻量原生风格控件。
- scripting pseudo browser 和 Win32 壳可以运行 `samples/apps/loose` 中的小型应用式示例。

## Timers

- `setTimeout(callback, ms)` 和 `clearTimeout(id)`。
- `setInterval(callback, ms)` 和 `clearInterval(id)`。
- Timer callback 必须是函数。字符串 eval timer 和额外 callback 参数被刻意排除。
- Timer 通过 `JerryScriptRuntime::pump_timers(now_ms, max_callbacks)` 由宿主泵动，
  因此嵌入式移植层可以提供自己的时钟源和单帧预算。
- pseudo browser 支持 `--pump-timers ms`，用于非交互验收。
- Win32 browser shell 通过桌面 `WM_TIMER` 泵动 timer，并在 callback 弄脏 DOM 后重绘。

## Document Script Loading

- scripting 构建会从解析后的文档中收集 classic inline `<script>`，并按 DOM 顺序执行。
- 本地外部 classic script（`<script src="...">`）通过壳层提供的 callback 加载。
  核心仍不执行文件或网络 I/O。
- `type="module"` 和其他非 classic script 类型会跳过。
- `jellyframe_pseudo_browser` 和 `jellyframe_win32_browser` 会自动执行文档脚本；
  命令行 `--script file.js` 仍作为额外桌面验收脚本保留。
- 网络加载、ES modules、dynamic import 和完整 HTML loading algorithm 继续排除。

## Embedded-App DOM Helpers

- 面向嵌入式 app 的 DOM helpers：
  - `element.children`
  - `element.parentElement`
  - `element.matches(simpleSelector)`
  - `element.closest(simpleSelector)`
  - 面向已有 `data-*` 属性的 `element.dataset` 快照 property
  - 小型 inline-style 属性集合 `element.style`
  - `element.hidden` 和 `element.disabled`
- `matches`/`closest` 支持的 selector 刻意保持很小：tag、`.class`、`#id`、
  `[attr]` 和 `[attr=value]`。Descendant/child combinator 目前仍只在 CSS 中支持。
- native input dispatch 会把 `pointerdown`、`pointerup`、`touchstart` 和 `touchend`
  作为 mouse-like event 暴露，便于可穿戴壳实现按下反馈。
- disabled 表单控件不会接收文本输入、range 移动或 activation。
- 文本检索式兼容性扫描已弃用。脚本相关 diagnostics 应来自真正处理 app 的 package loader、
  JerryScript runtime 和 DOM/event binding 代码路径。

## 暂不支持

- 完整 selector API，例如 `querySelector` / `querySelectorAll`。
- 通过任意新 key 动态创建 `dataset` property 或反向修改 native attribute。
- 超出单次求值范围的 promise/job pump。
- 网络、模块、dynamic import、storage、canvas 和 Web Components。

## 嵌入式策略

脚本桥接必须保持可选、显式、有界：

- 核心 HTML/CSS/rendering 必须能在没有 JerryScript 时构建。
- native wrapper 不拥有 DOM node。
- 每一个被保留的 `jerry_value_t` 都必须有清晰释放路径。
- detached DOM node 必须能通过 runtime statistics 观察，便于 port 审计脚本密集 UI 的内存行为。
- 脚本触发的重绘应消费 dirty flags，并合并重复工作。
- 只有当 C++ 核心能可预测地兑现行为时，才增加对应 API。
