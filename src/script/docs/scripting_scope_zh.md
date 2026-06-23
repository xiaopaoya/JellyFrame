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
- `jellyframe_win32_browser --script file.js` 用于桌面验收。
- 可选执行 watchdog：当 `JerryScriptRuntimeOptions` 或 `HostBudgets` 将
  `max_execution_check_count` 设为大于 0，且链接的 JerryScript 使用
  `JERRY_VM_HALT=ON` 构建时，失控的 eval 和 JS callback 会被中断，并抛出稳定的
  `script execution budget exceeded` 异常。若 JerryScript 未启用该特性，runtime 会报告
  watchdog 不可用，脚本按旧路径运行。
- `ScriptEvaluationResult::status` 会报告 `Ok`、`Exception` 或
  `ExecutionBudgetExceeded`。不返回 `ScriptEvaluationResult` 的 callback 路径会设置一个 sticky flag，
  由 `take_execution_watchdog_interrupt()` 消费，宿主可据此 kill/recover 当前 app，而不需要解析错误字符串。

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
- `jellyframe_win32_browser --script file.js` 会在执行脚本前绑定解析后的页面 DOM，
  因此脚本 mutation 会影响最终渲染输出。

## Events

- `node.addEventListener(type, callback, options)`。
- `node.removeEventListener(type, callback)`。
- listener options：boolean capture，以及对象形式 `{ capture, once }`。
- Event object 字段：`type`、`target`、`currentTarget`、`eventPhase`、`bubbles`、
  `cancelable`、`defaultPrevented`，以及适用时的鼠标坐标/按键、modifier keys 和滚轮 delta。
- 基础 `Event` 不会伪装成 `MouseEvent`。只有 native input 事件以及真正的
  `MouseEvent` / `WheelEvent` 才会暴露坐标、按键或滚轮字段。
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
- Win32 壳可以运行 `samples/apps/loose` 中的小型应用式示例。

## Timers

- `setTimeout(callback, ms)` 和 `clearTimeout(id)`。
- `setInterval(callback, ms)` 和 `clearInterval(id)`。
- Timer callback 必须是函数。字符串 eval timer 和额外 callback 参数被刻意排除。
- Timer 通过 `JerryScriptRuntime::pump_timers(now_ms, max_callbacks)` 由宿主泵动，
  因此嵌入式移植层可以提供自己的时钟源和单帧预算。
- Timer callback 使用与直接 eval 相同的可选执行 watchdog。
- Win32 browser shell 通过桌面 `WM_TIMER` 泵动 timer，并在 callback 弄脏 DOM 后重绘。

## Animation Frames

- JerryScript 构建中暴露 `requestAnimationFrame(callback)` 和
  `cancelAnimationFrame(id)`。
- Callback 是 one-shot，通过
  `JerryScriptRuntime::pump_animation_frame(now_ms, max_callbacks)` 由宿主泵动。
- Callback 会收到宿主毫秒时间戳。它应修改 DOM/style，并让 dirty flags 驱动 repaint。
- 当 app 处于后台、suspended、息屏或低功耗状态时，宿主可以把 animation callback/FPS 预算设为 0。
- Render core 已支持 CSS `transition` 子集：`opacity`、`transform:
  translate()/scale()`、`background-color` 和 `color`，由 `AnimationTimeline`
  与 animation dirty-region helper 推进。它也支持同一属性集合上的有界
  `@keyframes` / `animation-*` from/to 子集。需要精确逐帧控制时使用 rAF。

## Document Script Loading

- scripting 构建会从解析后的文档中收集 classic inline `<script>`，并按 DOM 顺序执行。
- 本地外部 classic script（`<script src="...">`）通过壳层提供的 callback 加载。
  核心仍不执行文件或网络 I/O。
- `type="module"` 和其他非 classic script 类型会跳过。
- `jellyframe_win32_browser` 会自动执行文档脚本；命令行 `--script file.js`
  仍作为额外桌面验收脚本保留。
- 网络加载、ES modules、dynamic import 和完整 HTML loading algorithm 继续排除。

## Runtime Data APIs

可选网络、app 私有 KV storage 和 system status events 的 JS 暴露形状记录在
`src/app_runtime/docs/runtime_data_api_zh.md`。

- 宿主绑定 network service 后暴露：异步 `XMLHttpRequest` V0 子集，包含
  `new XMLHttpRequest()`、async `GET` `open()`、`send()`、`abort()`、`readyState`、
  `status`、`responseText`、`responseURL` 和
  `onreadystatechange/onload/onerror/ontimeout/onabort/onloadend` callback 属性。
- 未绑定 host network service 时不暴露 `XMLHttpRequest`。App 应使用
  `typeof XMLHttpRequest === "function"` 做能力检测。
- 回调只在宿主把 network completion 泵回 UI/main task 后执行；worker 不直接调用 JS。
- scripting 构建中的 Win32 browser shell 绑定 debug `NetworkFetchMock`，用于桌面验证 completion
  分发模型，不代表核心包含真实网络栈。
- 已暴露：宿主显式绑定非阻塞 `AppLocalStorageShadow` 时提供极小 `localStorage` 子集：
  `getItem`、`setItem`、`removeItem`、`clear`、`key` 和 `length`。未绑定 shadow 时不暴露
  `localStorage`。
- 宿主绑定 audio adapter 后暴露：宿主可选的极小 `Audio` 子集。App 可写
  `new Audio(src)`，读写 `src`/`volume`，调用 `play()`，以及调用第一版 no-op 的
  `pause()`。未绑定 host audio adapter 时不暴露 `Audio`。如果宿主拒绝 source，
  `play()` 会抛错并向已注册 handler 派发 `error`。`onended`/`onerror` 以及面向
  `ended` 和 `error` 的 `addEventListener`/`removeEventListener` 已作为第一版状态事件子集支持。
  V0 每种事件保留一个函数 listener 加一个 `on*` property slot。暂不承诺完整
  `HTMLAudioElement`、Promise 或 streaming 状态。
- 已暴露：`navigator.onLine`、`window.addEventListener` / `removeEventListener`
  的 `online` / `offline` 系统状态事件子集、`document.hidden`、`document.visibilityState`
  和 `document` 的 `visibilitychange`，用于 accepted host system events。Win32 壳可用
  `Ctrl+F6`/`Ctrl+F7`/`Ctrl+F8` 注入 fake event。
- `fetch()` 等 Promise/microtask 有界后再考虑；battery API 不进入 V0。

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
- `fetch()`、模块、dynamic import、`sessionStorage`、IndexedDB、cookie、完整
  `HTMLAudioElement`、超出 `online`/`offline` 的完整 `Window`/`EventTarget` 语义、canvas 和
  Web Components。

## 嵌入式策略

脚本桥接必须保持可选、显式、有界：

- 核心 HTML/CSS/rendering 必须能在没有 JerryScript 时构建。
- native wrapper 不拥有 DOM node。
- 每一个被保留的 `jerry_value_t` 都必须有清晰释放路径。
- 产品级 scripting 构建建议用 `JERRY_VM_HALT=ON` 编译 JerryScript，并从
  `HostBudgets` 派生有限执行预算。
- detached DOM node 必须能通过 runtime statistics 观察，便于 port 审计脚本密集 UI 的内存行为。
- 脚本触发的重绘应消费 dirty flags，并合并重复工作。
- 只有当 C++ 核心能可预测地兑现行为时，才增加对应 API。
