# 脚本能力范围

WearWeb 的脚本能力会分阶段推进。目标是让嵌入式 app UI 真正可用，而不是一次性继承完整浏览器
API 表面。

## M2 支持范围

- 由 `WEARWEB_BUILD_SCRIPTING=ON` 控制的可选 `wearweb_script` target。
- `JERRYSCRIPT_ROOT` 可以指向官方 JerryScript checkout，例如 `third_party/jerryscript`。
- `JerryScriptRuntime` 管理 JerryScript 生命周期。
- `eval(source, source_name)` 执行 classic JavaScript 源码。
- 返回字符串化后的成功结果，或字符串化后的异常结果。
- 同一进程内可重复初始化/清理；当前实现同一时间只允许一个 runtime 存活。
- `wearweb_pseudo_browser --script file.js` 用于桌面验收。

## M3 支持范围

- 宿主绑定 native DOM tree 后，提供全局 `window` 和 `document` 对象。
- `document.getElementById(id)`。
- `document.createElement(tag)`。
- `document.createTextNode(text)`。
- `node.appendChild(child)`，并处理 detached node 的所有权转移。
- `node.removeChild(child)`，返回的 wrapper 仍可继续使用。
- `element.setAttribute(name, value)`。
- `element.getAttribute(name)`。
- `node.textContent` getter/setter。
- `wearweb_pseudo_browser --script file.js` 会在执行脚本前绑定解析后的页面 DOM，
  因此脚本 mutation 会影响最终渲染输出。

## M4 支持范围

- `node.addEventListener(type, callback, options)`。
- `node.removeEventListener(type, callback)`。
- listener options：boolean capture，以及对象形式 `{ capture, once }`。
- Event object 字段：`type`、`target`、`currentTarget`、`eventPhase`、`bubbles`、
  `cancelable`、`defaultPrevented`，以及适用时的鼠标坐标/按键、modifier keys 和滚轮 delta。
- Event object 方法：`preventDefault`、`stopPropagation` 和 `stopImmediatePropagation`。
- JavaScript listener 复用现有 C++ capture/target/bubble 事件流，并可在 native input dispatch
  过程中修改 DOM。
- scripting 构建中的 Win32 browser shell 支持 `--script file.js`，并在脚本事件回调弄脏 DOM 后重绘。

## 暂不支持

- `getElementById` 之外的 DOM selector。
- 表单控件的 JavaScript 属性。
- 计时器，以及超出单次求值范围的 promise/job pump。
- HTML 中的 inline `<script>` 和脚本加载流程。
- 网络、模块、dynamic import、storage、canvas 和 Web Components。

## 嵌入式策略

脚本桥接必须保持可选、显式、有界：

- 核心 HTML/CSS/rendering 必须能在没有 JerryScript 时构建。
- native wrapper 不拥有 DOM node。
- 每一个被保留的 `jerry_value_t` 都必须有清晰释放路径。
- 脚本触发的重绘应消费 dirty flags，并合并重复工作。
- 只有当 C++ 核心能可预测地兑现行为时，才增加对应 API。
