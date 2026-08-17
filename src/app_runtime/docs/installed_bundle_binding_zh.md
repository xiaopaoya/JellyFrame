# 已安装 Bundle 绑定

> 最后更新：2026-08-18；适用版本：0.6.0-dev；状态：平台无关契约

`app_installed_bundle.*` 是 Device OS 已提交 app library 与
`AppRuntimeHost` 之间的窄边界。它不解析文件系统、不复制桌面 package loader、不拥有 registry
发布，也不创建第二套 app runtime。它只给 port 一条把已校验 `.jfapp` bundle 接入正常 lifecycle
与资源加载路径的安全方式。

## 所有权

`AppInstalledBundleProvider` 由 Device OS 的 registry/storage owner 实现。只有已由
`inspect_device_bundle(...)` 接受且已经 committed、不可变的记录，才可通过
`acquire_installed_bundle(app_id, lease)` 返回 lease。不得暴露 staging bytes、可变 registry record
或借用的 transport buffer。

`AppInstalledBundleLease` 提供有界、同步的 `read_at()` 与复制得到的
`DeviceBundleDescriptor`。provider 持有实际 partition、flash mapping 或 cache；Runtime 完成使用时
只调用一次 `release()`。lease 始终留在 App Runtime supervisor task，绝不能传给 script worker、
UI task、DMA queue 或 service worker。跨边界只能传 resource 的复制字节以及 value-only 的
frame/input/service 协议值。

provider 应为每次 acquire 返回可独立 release 的 lease。binding 也能容忍 provider 在原地 reload 时
复用当前 active lease：它会让该 lease 跨越旧 app teardown，直到 replacement 退出才 release。provider
不得为不同 app id 返回当前 active lease。

## 启动与恢复

installed app 必须由 `AppInstalledBundleBinding` 协调：

1. 在变更 active app 前先 acquire 并校验 lease。
2. 确认 descriptor identity 与请求的 app id 完全一致。若不一致，释放新 lease，当前 app 保持不变。
3. 通过 `AppRuntimeHost` 以 `app-switch` teardown 旧 app。
4. host teardown 完成后才释放旧 installed-bundle lease。
5. 创建新 `AppInstance`、保留其 lease，并通过 `read_active_resource(...)` 读取资源。

load failure、runtime fatal 或 budget recovery 时，调用
`recover_to_protected_launcher(host, reason)`。它先终止 active app、释放 lease，之后才调用
`AppProtectedLauncher`。launcher 不会拿到第三方 lease。若 launcher 返回 false，这是一条需要被
Device OS 记录并恢复的稳定失败，不能继续使用已经释放的 bundle。

active `AppInstalledBundleBinding` 存在时，不得混用直接的
`AppRuntimeHost::launch/terminate_current`。显式 binding 让 port 只有一种 teardown 顺序和 source
release 点。

## Port 责任

WS147/其他 port adapter 必须：

- 把 raw-partition read 及 cache/flash lease 留在 Device OS storage owner 内；
- 在 staging verification 中调用 `inspect_device_bundle`，传入目标的 bundle、resource count 与
  summary budget；
- 将已验证的 `DeviceBundleDescriptor` 和 committed record 一同存储，或在发放 lease 前重新构造并验证；
- JFDP/1 AppList/Recovery response 使用 `DeviceAppLibraryEntry` 与
  `DeviceRecoveryDetailPayload`，不得直接序列化 registry 结构；
- installed-bundle load 使用 binding，所有 failure fallback 使用 protected launcher callback；
- reboot recovery 时，只有全部 reader 离开 supervisor task 后才能释放或失效 storage lease。

该契约没有 filesystem、RTOS、panel、JerryScript、`Node*`、`LayerNode*`、`jerry_value_t` 或
port-private ABI 依赖。这样未来迁移 Device OS 时，不会让通用 App Runtime 变成 storage 实现。
