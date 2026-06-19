# Runtime Data API 规划

本文定义可选 runtime data services 未来暴露给 JavaScript 的形状。API 尚未暴露；这份文档用于让
C++ host-service 契约、Win32 壳和后续 JerryScript binding 收敛到同一个小型、有界模型。

## 原则

- service 不在 UI/main task 上执行慢工作。
- 每个操作都绑定 active `app_instance_id`。
- manifest capability 与 host/profile policy 必须同时允许该服务。
- 结果只在 UI/main task pump 到 accepted completion 或 system event 后派发。
- V0 不依赖 Promise/microtask。callback API 更容易在 JerryScript 和小 RTOS host 上做预算控制。
- 大数据继续由宿主持有，通过 handle 管理。JS 只接收小型复制字符串、状态码和短错误名。
- 旧 app instance 的 completion 只释放 handle，不会调用新 app 的 callback。

## 建议全局对象

binding 应暴露一个很小的命名空间：

```js
JellyFrame.fetchText(url, callback)
JellyFrame.storage.get(key, callback)
JellyFrame.storage.set(key, value, callback)
JellyFrame.storage.remove(key, callback)
JellyFrame.storage.clear(callback)
JellyFrame.system.on(type, callback)
JellyFrame.system.off(type, callback)
JellyFrame.system.snapshot()
```

`window.fetch`、同步 `localStorage`、IndexedDB 和浏览器 storage events 仍不在 V0 范围内。

## 网络

`JellyFrame.fetchText(url, callback)` 提交 `HostServiceJobKind::NetworkFetch`。

callback 形状：

```js
function callback(error, response) {
  // error 为 null 或 { code, message }
  // response 为 { status, contentType, text }
}
```

规则：

- V0 只规划 GET。
- 远程 HTML/CSS/script/image 仍禁止作为页面资源。
- URL 长度、响应字节、超时和并发请求数来自合成后的 `NetworkFetchPolicy`。
- 非 2xx HTTP status 不自动等同于传输错误；DNS/TLS/timeout/连接失败等由 host 通过 `error` 报告。

## App 私有 KV Storage

storage 保持异步且 app 私有：

```js
JellyFrame.storage.get("theme", function (error, value) {})
JellyFrame.storage.set("theme", "dark", function (error) {})
JellyFrame.storage.remove("theme", function (error) {})
JellyFrame.storage.clear(function (error) {})
```

规则：

- V0 的 value 是字符串，或由 binding 把 UTF-8 bytes 转成字符串。
- key 长度、单 value 字节数、item 数和总字节数来自合成后的 `AppPrivateKvPolicy`。
- 缺失 key 应回调 `null` value，而不是抛 fatal exception。C++ completion 可以仍使用 `Failed`，
  binding 负责映射成文档化的 miss 结果。
- 不实现同步 `localStorage`；flash/NVS/filesystem 写入绝不能阻塞 UI task。

## System Events

system state 由宿主通过 `AppSystemEventQueue` 注入。

```js
JellyFrame.system.on("battery", function (snapshot) {})
JellyFrame.system.on("network", function (snapshot) {})
JellyFrame.system.on("time", function (snapshot) {})
var snapshot = JellyFrame.system.snapshot()
```

snapshot 字段：

```js
{
  unixTimeMs,
  timezoneOffsetMinutes,
  batteryPercent,
  charging,
  networkOnline,
  screenOn,
  lowPowerMode
}
```

规则：

- `snapshot()` 返回 runtime binding 中保存的最新宿主批准快照，不是实时硬件对象。
- event callback 按帧预算派发。
- app 代码不能直接读取 RTC、Wi-Fi、电量计或电源管理驱动。

## 错误名

binding 应把 host status 映射为稳定短字符串：

| Host status | JS error code |
| --- | --- |
| `Unsupported` | `unsupported` |
| `BudgetExceeded` | `budget-exceeded` |
| `Timeout` | `timeout` |
| `Cancelled` | `cancelled` |
| `Failed` | `failed` |

这个映射刻意保持很小。详细平台错误码可以留在 diagnostics 中，或作为桌面调试时可选的 `hostCode` 字段。
