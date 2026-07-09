# App 生命周期

> 最后更新：2026-07-07；适用版本：0.5.0-dev

本文是 app 作者视角的 JellyFrame 生命周期说明，描述一个 app 在安装、启动、挂起、恢复、终止和卸载时
可以依赖什么行为。宿主和开发板移植实现细节见 `host_optional_services_zh.md`。

## 模型

JellyFrame V0 runtime 当前一次运行一个 active app instance。启动器、表盘和设置页也可以是
JellyFrame app，但产品宿主应把它们作为受信系统角色处理。第三方 app 不会获得裸文件系统、网络栈、
GPIO、显示屏、音频 codec 或传感器句柄。

每次启动都会生成新的 `app_instance_id`。宿主请求、service completion、system event、handle
和 app 字体资源都绑定到这个 instance。app 退出、崩溃、被策略杀死或被另一个 app 替换时，旧
completion 会被丢弃，handle 会被释放，不能再修改后续 app。

## 安装与删除

可安装 app 是 `.jfapp` bundle，或通过桌面 CLI/system shell 路径安装的源包。

- `jellyframe_cli.py check` 会在安装前验证 manifest、资源、capability intent、字体和渲染管线。
- `jellyframe_cli.py install --root app_dir --store store_dir` 会 staging app、验证、原子写入 registry，
  然后让启动器在后续帧刷新。
- 安装失败必须丢弃 staging 字节，并保留旧 registry。
- 卸载默认删除 app 私有数据。产品 shell 可以提供“保留数据”的用户选择。
- 删除 active app 前应先回到受信 launcher/system shell。

普通 app 内运行的 JavaScript 不能直接安装、挂载、删除或更新其他 app。未来系统组件可以通过受信
role 和宿主持有 broker 暴露这类流程，而不是提供通用浏览器 API。

受信 launcher/system-shell app 仍是普通 JellyFrame 包，只是 manifest 中使用系统角色，例如
`role: "launcher"`，并声明 `system.launcher`、`system.appManager` 等 capability。这些 capability
由宿主解释，不会生成普通第三方 app 可调用的 Web API。当前 Win32 示例 launcher 使用包内 HTML 加宿主注入的
app 列表 markup；其中的 `data-action` 按钮只会在 shell 处于 system-shell mode 时被消费。普通已安装 app
当然可以在自己的 UI 中使用 `data-*` 属性，但这些属性不能启动、启用、禁用、回滚或删除其他 app。

## 启动与帧循环

启动时，宿主加载包内资源，构建 DOM/style/layout/layer，并通过和 Win32 调试壳相同的渲染管线绘制首帧。
之后 app 会按当前 frame policy 接收输入、timer、animation frame 和宿主 service completion。

宿主每帧推荐顺序：

1. 接受属于 active instance 的 host completion 和 system event。
2. 执行有界 JavaScript callback 和 timer。
3. 应用 DOM/style 变更并计算 dirty region。
4. 只对需要的区域 layout/paint。
5. 把 dirty rectangles 或 full frame 提交给显示后端。

网络请求、图片解码、音频播放、文件访问和 bundle 安装等慢工作必须作为 UI task 外的宿主 job 执行。
completion 会在后续帧回到 active instance。

## 挂起、恢复与可见性

挂起是可恢复的状态切换，用于 app 后台、息屏和低功耗策略。它不是 teardown。

JellyFrame V0 将系统状态映射到一组很小的 web-like JavaScript 表面：

- `document.hidden`
- `document.visibilityState`
- `document.addEventListener("visibilitychange", ...)`
- `navigator.onLine`
- `window.addEventListener("online", ...)`
- `window.addEventListener("offline", ...)`

suspended 或 screen-off 时，宿主应停止前台输入、timer、`requestAnimationFrame` 和 present，
除非产品 profile 明确允许某个 service 继续活动。resume 后，宿主应在第一帧可交互前安排 repaint，
并可重新注入 network/visibility 快照。

电量状态和详细低功耗状态在 V0 不暴露给 app JavaScript，它们仍是宿主策略输入。

## 运行时服务

App 通过 manifest capability 请求服务，所选 target profile 也必须允许对应服务。

- `compute.jobs` 表示有界的具名宿主计算工作。V0 仅是 port/system contract：不暴露 JavaScript Worker、任意后台 callback 或 message-port API。
- `network.fetch` 启用异步 `XMLHttpRequest` GET V0 子集。
- `storage.kv` 只在宿主绑定非阻塞 app 私有 shadow 时启用极小 `localStorage` 子集。
- `media.audio.playback` 启用宿主可选的 `Audio()` V0 子集。
- `location.position` 在宿主绑定 location service 时启用
  `navigator.geolocation.getCurrentPosition(...)`。
- sensor capability 名称只表达意图，sensor JavaScript API 在 V0 仍延后。

后台 service intent 写在 `backgroundServices`，它本身不授予权限。宿主会结合 manifest intent、用户授权、
target profile 和电源状态，决定 network、audio、sensor 或 location work 是否可以在 suspended
或 screen-off 时继续运行。

## 存储生命周期

`localStorage.setItem(...)` 只表示 app 私有 RAM shadow 已更新，不代表 flash/NVS/filesystem
持久化已经完成。持久化由宿主持有，并遵循 storage lifecycle policy：

- 正常 exit/update 应尽量 flush pending writes。
- crash、budget recovery 和 memory pressure 可以 drop pending writes。
- uninstall 默认 drop pending work 并删除持久数据。
- 宿主应优先保证可预测恢复，而不是为了 flash 写入阻塞 UI task。

App 应按“持久化可能失败”的方式编写。Quota 和 lifecycle 失败可通过 package diagnostics 与 Win32
shell 输出观察。

## 失败与恢复

runtime 可以用稳定 reason 终止 active app，例如：

- `user-kill`
- `script-watchdog`
- `budget-exceeded`
- `load-failure`
- `system-policy`

终止会取消当前请求、丢弃 stale completion、释放 host handle 和 app 字体资源，然后回到受信
launcher/system shell。app 可以失败；runtime、launcher 和其他 app 必须继续运行。任何不修改固件的操作
都应有不依赖重新烧写设备的 fallback。

## Win32 调试流程

对 app 作者而言，Win32 壳是首选交互式调试宿主：

```powershell
python tools\jellyframe_cli.py doctor --build-dir build\Release
.\build\Release\jellyframe_win32_browser.exe --app samples\apps\packages\watch_weather
```

常用生命周期检查：

- `Ctrl+F6`：切换 network online/offline。
- `Ctrl+F7`：切换 screen visibility。
- `Ctrl+F8`：切换 low-power visibility。
- `--system-survival-smoke N`：反复启动坏 app，验证 launcher recovery。
- `--registry-store build\installed_apps`：使用 installed-app registry 和 sample launcher 路径。

Frame script 也可以注入同类 network/visibility/low-power event，用于确定性 CI 截图。
