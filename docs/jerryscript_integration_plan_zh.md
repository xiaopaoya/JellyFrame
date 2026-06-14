# JerryScript 接入计划

WearWeb 应当把 JerryScript 接成一个小型宿主运行时，而不是试图复制完整浏览器 Web API。
绑定层只暴露核心已经真实支持的能力，并继续遵守 C++ 引擎的嵌入式约束：内存有界、所有权明确、不隐式联网、
重绘调度可预测。

## 资料要点

JerryScript 官方 API 示例展示了基本嵌入生命周期：用 `jerry_init` 初始化，用 `jerry_eval`
或 `jerry_parse` + `jerry_run` 执行脚本，检查异常值，释放每个返回的 `jerry_value_t`，最后用
`jerry_cleanup` 清理。官方 extension handler 文档说明了如何把 C 原生函数注册成 JS 可见属性。
引用计数文档对绑定层很重要，因为返回的 `jerry_value_t` 会携带新的 live reference，宿主必须释放。

参考：

- <https://jerryscript.net/api-example/>
- <https://jerryscript.net/api-reference/>
- <https://jerryscript.net/ext-reference-handler/>
- <https://jerryscript.net/reference-counting/>
- <https://jerryscript.net/debugger/>

## 当前就绪程度

已经具备：

- 由 `WEARWEB_BUILD_SCRIPTING=ON` 控制的 M2 runtime shell：初始化、清理、`eval`、
  source name、结果字符串化和异常报告。
- M3 最小 DOM binding：`window`、`document`、`getElementById`、`createElement`、
  `createTextNode`、`appendChild`、`removeChild`、`setAttribute`、`getAttribute`
  和 `textContent`。
- M4 事件桥：`addEventListener`、`removeEventListener`、event object、default prevention
  和 propagation control，并复用现有 C++ event dispatch 路径。
- M5 表单控件属性：`value`、`checked`、`selectedIndex`，以及面向宿主驱动控件的
  JavaScript 可观察 `input` / `change` 事件。
- M6 宿主泵动 timer：`setTimeout`、`clearTimeout`、`setInterval`、`clearInterval`，
  并通过 `pump_timers` 控制 callback budget。
- DOM tree 构建和容错 HTML 解析。
- DOM mutation 原语：插入/删除子节点、设置/删除属性、更新 text content。
- tree、attribute、text、style、layout dirty flags。
- 捕获、目标、冒泡事件派发。
- 常见表单控件核心状态。
- pointer、wheel、text input 和简单 key input 路径。
- Win32 验证壳可以在状态变化后重绘同一份 DOM。

尚未具备：

- HTML 中的自动脚本加载。
- wrapper 缓存；当前 wrapper 是短生命周期的 native DOM node 视图。
- 明确子集之外更宽的 DOM 属性/attribute alias。

## 架构

```text
WearWebScriptRuntime
  管理 JerryScript 引擎生命周期
  管理 wrapper map 和 JS callback reference
  |
  +-- Window binding
  +-- Document binding
  +-- Element/Text binding
  +-- Event binding
  +-- Timer/task queue
  |
DOM + style + layout + layer + renderer
```

运行时应当是可选构建目标：

- `WEARWEB_BUILD_SCRIPTING=OFF`，在 bridge 稳定前默认关闭。
- 核心 HTML/CSS/rendering 不包含 JerryScript 头文件。
- scripting target 链接 JerryScript，并依赖 `wearweb_core`。

## 绑定原则

- JS wrapper 是 DOM node 的轻量 native 视图。当前实现按需创建短生命周期 wrapper；
  wrapper cache 是未来的内存/性能权衡，不属于当前契约。
- JS wrapper 不拥有 DOM node；DOM tree 拥有 node。
- wrapper finalizer 释放 JerryScript reference，但不删除 DOM node。
- native event listener 显式持有 `jerry_value_t` reference，在移除 listener 或销毁 runtime 时释放。
- 所有公开 binding function 都返回明确的成功/错误值；C++ 异常不能穿过 JerryScript 边界。
- 脚本执行和事件回调后，由宿主循环消费 dirty flags。

## 里程碑

### M2：Runtime Shell

状态：已作为可选构建目标实现。`wearweb_script` 只在 `WEARWEB_BUILD_SCRIPTING=ON`
时链接 JerryScript；`wearweb_core` 仍不包含 JerryScript 头文件或库。
`wearweb_pseudo_browser --script` 可以执行一个外部脚本文件，并打印结果或异常，供验收测试使用。

创建 `WearWebScriptRuntime`：

- `initialize()` / `shutdown()`。
- `eval(std::string_view source, std::string_view name)`。
- 严格的 `jerry_value_t` RAII helper，负责释放引用。
- 脚本异常到日志的路径。

验证：

- 脚本能运行并设置一个全局值。
- 所有 JerryScript 返回值都被释放。
- 同一进程内可以反复初始化和清理 runtime。

### M3：最小 DOM 对象

状态：已面向宿主驱动的同步脚本实现。JavaScript 创建的 detached node 先由 runtime 持有；
`appendChild` 会把所有权转移到 native DOM tree；`removeChild` 会把所有权移回 runtime，
因此返回的 wrapper 仍可继续使用。

暴露：

- `window`
- `document`
- `document.getElementById(id)`
- `document.createElement(tag)`
- `document.createTextNode(text)`
- `node.appendChild(child)`
- `node.removeChild(child)`
- `element.setAttribute(name, value)`
- `element.getAttribute(name)`
- `node.textContent`

验证：

- JS 修改 text content 后，宿主能观察到 `DomDirtyText | DomDirtyLayout`。
- JS 创建元素并插入后，管线能重绘该元素。

### M4：事件桥

状态：已支持由 C++ 事件系统派发的同步 callback。JavaScript listener reference 由
`JerryScriptRuntime` 持有；runtime 关闭时会先从 native `EventTarget` 移除 listener，
再在 `jerry_cleanup` 前释放对应 `jerry_value_t`。

暴露：

- `addEventListener(type, callback)`
- `removeEventListener(id)` 或一个受限等价接口
- event object：`type`、`target`、`currentTarget`、`preventDefault`、`stopPropagation`

验证：

- JS click listener 可以修改 DOM。
- listener 移除后 callback reference 被释放。
- 重入事件派发要么支持，要么明确阻止。

### M5：表单控件属性

状态：已面向宿主驱动控件实现。表单状态保存在平台无关核心中，并通过轻量 property accessor
暴露给 JavaScript。native input dispatch 可以同步触发 JS listener 可观察的 `input` 和 `change` 事件。

暴露：

- `input.value`
- `textarea.value`
- `checkbox.checked`
- `radio.checked`
- `select.value`
- `select.selectedIndex`

验证：

- 用户输入更新 JS 可见状态。
- JS 修改状态后轻量原生控件外观会重绘。
- JS 可以观察 `input` 和 `change` 事件。

### M6：任务队列和计时器

状态：已实现为宿主泵动 timer。runtime 保存被保留的 callback reference，并暴露
`pump_timers(now_ms, max_callbacks)`，使嵌入式宿主可以用自己的 tick 源和固定单帧预算驱动 callback。

暴露：

- `setTimeout(callback, ms)`
- `clearTimeout(id)`
- `setInterval(callback, ms)`
- `clearInterval(id)`
- 由宿主壳驱动的小型 timer queue

验证：

- timer callback 在宿主 tick 后运行。
- dirty flags 合并，使多次 JS mutation 触发一次重绘。

### M7：脚本加载

支持：

- inline classic `<script>`
- 可选的壳层本地脚本加载 callback

暂不支持：

- 网络 fetch
- ES modules
- dynamic import
- 完整浏览器 loading algorithm

## 第一个 Demo 目标

HTML：

```html
<button id="count">0</button>
<script>
  var n = 0;
  document.getElementById("count").addEventListener("click", function () {
    n += 1;
    document.getElementById("count").textContent = String(n);
  });
</script>
```

预期行为：

- 页面渲染按钮。
- 点击通过 C++ 事件系统进入 JS。
- JS 修改 `textContent`。
- dirty flags 触发宿主重绘。
- 按钮文字递增。

## 主要风险

- event listener 或 wrapper map 泄漏 `jerry_value_t` reference。
- DOM node 被移除时 wrapper 生命周期不匹配。
- 暴露了核心无法兑现的 API。
- 每个小 mutation 都重建过多管线。
- 在嵌入式 runtime 稳定前过早追求完整 Web 兼容。

## 推荐下一步

继续 M7：支持 inline classic `<script>` 和壳层提供的本地脚本加载 callback，同时继续把网络加载、
modules 和 dynamic import 排除在嵌入式核心之外。
