# Runtime Data API 规划

本文定义可选 runtime data services 未来如何暴露给 JavaScript。API 尚未暴露。面向用户的语法应尽量保持为
Web 平台 API 的文档化子集，避免 app 作者学习 JellyFrame 专用的数据 API。

## 原则

- 优先使用标准名称、对象形状和事件名。
- 如果某个标准 API 还不能被可预测地兑现，就先不要暴露。
- JellyFrame 内部可以有 C++ helper，但 app JavaScript 不应依赖自定义 `JellyFrame.*`
  数据 API 来完成常见 Web 概念。
- service 不在 UI/main task 上执行慢工作。
- 每个操作都绑定 active `app_instance_id`。
- manifest capability 与 host/profile policy 必须同时允许该服务。
- 结果只在 UI/main task pump 到 accepted completion 或 system event 后派发。
- 大数据继续由宿主持有，通过 handle 管理。JS 只接收有界复制字符串、状态码和短错误名。
- 旧 app instance 的 completion 只释放 handle，不会调用新 app 的 callback。

## 网络

V0 首选用户 API：异步 `XMLHttpRequest` 子集。

`fetch()` 应等待 JellyFrame 具备有界 Promise/microtask 方案后再暴露。自定义 callback helper
更容易实现，但会制造非标准写法，因此弃用该方向。

当前平台无关地基：`AppXmlHttpRequest` 已在 `NetworkFetchMock`/host completion 之上实现 V0 XHR
状态机。它还不是 JS binding；它提供了未来 `XMLHttpRequest` 构造器可以包装的已测试生命周期。

计划中的 XHR 子集：

```js
var xhr = new XMLHttpRequest();
xhr.open("GET", "https://api.example.com/weather", true);
xhr.timeout = 3000;
xhr.onload = function () {
  console.log(xhr.status, xhr.responseText);
};
xhr.onerror = function () {};
xhr.ontimeout = function () {};
xhr.send();
```

V0 支持面应限制在：

- `new XMLHttpRequest()`
- `open(method, url, async)`，仅支持 async `GET`
- `send()`
- `abort()`
- `timeout`
- `readyState`
- `status`
- `responseText`
- `responseURL`
- `onreadystatechange`
- `onload`
- `onerror`
- `ontimeout`
- `onabort`
- `getResponseHeader("content-type")`

规则：

- `network.fetch` 仍作为 manifest capability 名称，对应宿主服务能力；但 JS 作者入口优先做 XHR 子集。
- 远程 HTML/CSS/script/image 仍禁止作为页面资源。
- URL 长度、响应字节、超时和并发请求数来自合成后的 `NetworkFetchPolicy`。
- 非 2xx HTTP status 不自动等同于传输错误。
- POST、自定义 header、credentials、redirect、streaming、binary response type 和 upload progress 延后。

## App 私有 Storage

V0 首选用户 API：极小 `localStorage` 子集，但前提是宿主能提供 app 私有 RAM shadow，或以其他方式保证
getter/setter 不阻塞 flash/filesystem I/O。

如果目标 profile 无法保证这一点，storage 应继续不暴露，而不是新增自定义 async API。

计划子集：

```js
localStorage.setItem("theme", "dark");
var theme = localStorage.getItem("theme");
localStorage.removeItem("theme");
localStorage.clear();
```

V0 支持面应限制在：

- `localStorage.getItem(key)`
- `localStorage.setItem(key, value)`
- `localStorage.removeItem(key)`
- `localStorage.clear()`
- `localStorage.length`
- `localStorage.key(index)`

规则：

- value 是字符串，与 Web Storage 模型一致。
- storage 按 app 私有隔离，不能访问其他 app 命名空间。
- key 长度、value 字节数、item 数和总字节数来自合成后的 `AppPrivateKvPolicy`。
- 同步调用必须命中小型内存 shadow。宿主 flash/NVS/filesystem 写入通过 async service path 调度，
  并由宿主策略完成落盘/恢复。
- quota 失败应尽量抛小型 `QuotaExceededError` 类异常；早期 bring-up 无法完整异常化时，至少提供
  diagnostics warning。
- `sessionStorage`、storage events、IndexedDB、cookie 和 Cache API 不进入 V0。

## System State

系统状态应优先映射到已有 Web 邻近概念：

- `navigator.onLine`
- `window` 的 `online` / `offline` 事件
- `document.hidden`
- `document.visibilityState`
- `document` 的 `visibilitychange` 事件
- 后续如需要，再考虑生命周期相关的 `pagehide` / `pageshow`

电量和低功耗状态没有一个足够安全、现代、普遍的基线。Battery Status API 历史上存在，但有隐私敏感问题，
不适合作为默认入口。V0 中电量/充电/低功耗快照继续保留在 C++ host event queue；只有当产品 profile
明确选择兼容 surface 时，才考虑暴露给 app JavaScript。

平台无关来源仍是 `AppSystemEventQueue`。JS binding 应尽量把 accepted events 映射到上述标准子集。

## 错误名

内部 host status 仍可映射成稳定短字符串，用于 diagnostics：

| Host status | Diagnostic code |
| --- | --- |
| `Unsupported` | `unsupported` |
| `BudgetExceeded` | `budget-exceeded` |
| `Timeout` | `timeout` |
| `Cancelled` | `cancelled` |
| `Failed` | `failed` |

详细平台错误码可以留在 diagnostics 中，或作为可选 host debug 字段。它们不应成为 app 作者必须学习的语法。
