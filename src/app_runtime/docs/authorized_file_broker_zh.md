# 授权文件 Broker

> 最后更新：2026-07-10；适用版本：0.5.0-dev

JellyFrame 不向普通 app 暴露裸文件系统 API。默认持久化模型仍是 app-private storage。
通用文件访问只保留给系统组件、文件管理器 app，或用户明确批准的 app 操作，并且必须通过宿主持有的
broker。

## 能力名

标准 manifest capability 名称为：

- `file.read`：读取用户批准的文件或宿主逻辑路径。
- `file.write`：通过宿主 staging 写入或替换用户批准的文件。
- `file.manage`：为受信文件管理器和系统组件流程提供 list、rename、delete、create。

这些名称只声明意图。host/profile 仍必须授予同名能力；每次操作仍必须来自用户批准，或来自受信系统组件。

## V0 UX 与 API 决策

V0 继续把通用文件访问保留为 host/system-shell broker 契约，不作为普通 app JavaScript API 暴露。

普通 app 默认应使用 app-private storage。只有当用户通过产品 UI 发起明确文件动作时，例如
“导入这个文件”、“导出这份日志”或“替换选中的图片”，host 才记录已批准的 operation、逻辑路径范围、
字节预算，以及授权是一次性还是持久授权，然后提交有界 broker job。App 仍然不会拿到原生路径或
filesystem handle。

受信系统组件和文件管理器 app 只有在 host 以对应角色启动它们时，才可以使用
`trusted_system_component=true`。这个角色属于产品策略，不是安装式第三方 app 可以自行设置的字段。

所有会修改数据的操作都应先 staging，再在校验后 commit。write、rename 和 delete 失败时必须 rollback
或保持旧 entry 不变。用户取消、timeout、介质移除、quota 失败和不支持的操作都应返回稳定 broker
status，不能导致 runtime 崩溃，也不能要求重新烧写固件才能恢复。

未来如要增加 JavaScript surface，应等这套 UX 验证后再加入。它更适合是一个绑定 manifest capability
和 host 授权的小型 async broker API，而不是复制桌面浏览器 File System Access。

## 核心契约

`authorized_file_broker.h` 提供平台无关的校验层：

- `AuthorizedFilePolicy`：host/profile 对 read/write/manage 的 gate，以及路径和字节预算。
- `AuthorizedFileRequest`：请求操作、规范化逻辑路径、rename 的可选第二路径、传输字节数和授权标记。
- `validate_authorized_file_request(...)`：返回稳定状态，例如 `user-approval-required`、
  `capability-denied`、`invalid-path`、`traversal-rejected`、`byte-budget-exceeded`。

核心不打开文件、flash partition 或 block device。有效请求应作为宿主持有的异步 job 提交，
即 `HostServiceJobKind::AuthorizedFile`，再回到 UI task 完成。App 不得获得裸 filesystem handle；
任何结果 handle 都应是受预算限制、由宿主持有的 broker result。

## 路径规则

Broker path 是逻辑绝对路径，不是原生文件系统路径：

- 必须以 `/` 开头。
- 不能包含 `://`、`//`、`\`、控制字符、`.` 或 `..` 组件。
- 除非未来 broker 版本显式定义 root directory 操作，否则不能以 `/` 结尾。
- 必须在 `AuthorizedFilePolicy::max_path_bytes` 以内。

宿主在校验后再把这些逻辑路径映射到产品存储、媒体分区或用户挂载点。

## 宿主职责

host 或 port 层必须实现：

- 用户授权或受信组件策略。
- 逻辑路径到真实存储的映射。
- write、rename、delete 的 staging 与 rollback。
- 异步 completion、timeout 和 cancellation。
- 字节预算与稳定错误映射。
- 所有不修改固件的失败路径都要有 fallback，恢复不能依赖重新烧写固件。

目前仍不暴露 JavaScript 文件 API。未来 JS 绑定应等 broker 生命周期和授权 UX 在 Win32 壳中验证后再加入。

## Win32 验收

Win32 壳提供确定性的 broker smoke test：

```powershell
.\build-script\Release\jellyframe_win32_browser.exe --authorized-file-smoke out\file_broker
```

它会验证：未批准写入不会改变另一个逻辑 app 路径；路径穿越会被拒绝；staged write 只在校验后提交；
模拟 commit 失败会保留旧文件；manage 操作与 read/write 分开 gate。
