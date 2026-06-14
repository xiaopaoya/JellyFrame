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

## 暂不支持

- `window` 和 `document`。
- JavaScript 侧 DOM wrapper、选择器和 DOM mutation。
- JavaScript event listener。
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
