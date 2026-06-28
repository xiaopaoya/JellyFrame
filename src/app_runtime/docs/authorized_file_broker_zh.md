# 授权文件 Broker

JellyFrame 不向普通 app 暴露裸文件系统 API。默认持久化模型仍是 app-private storage。
通用文件访问只保留给系统组件、文件管理器 app，或用户明确批准的 app 操作，并且必须通过宿主持有的
broker。

## 能力名

标准 manifest capability 名称为：

- `file.read`：读取用户批准的文件或宿主逻辑路径。
- `file.write`：通过宿主 staging 写入或替换用户批准的文件。
- `file.manage`：为受信文件管理器和系统组件流程提供 list、rename、delete、create。

这些名称只声明意图。host/profile 仍必须授予同名能力；每次操作仍必须来自用户批准，或来自受信系统组件。

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
