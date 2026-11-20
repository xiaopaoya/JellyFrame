# 组件兼容性矩阵

> 最后更新：2026-11-20；适用版本：0.6.0-dev。

这份矩阵记录三个计划中的产品边界之间的兼容性证据。它不同于 HTML/CSS
能力全表：能力全表描述 app 可见行为，本文件描述哪个构建产物可以消费哪个
构建产物。

## 当前矩阵

| 消费者 | 提供者 | 版本 / ABI | 状态 | 证据与限制 |
| --- | --- | --- | --- | --- |
| JellyFrame App Runtime | 源码内 `jellyframe_render_core` | 同一 checkout | `verified` | 默认桌面 Release/Debug 和非 scripting CI CTest。适合同步修改 Core 与 Runtime。 |
| JellyFrame App Runtime | 解压后的 Core 源码归档 | `0.12.1` / ABI `11` | `verified` | 源码 override 通过 14/14 项专项回归。`JELLYFRAME_RENDER_CORE_SOURCE_DIR` 与 package 模式互斥。 |
| Render Core standalone 测试 | 无 Runtime、无 JerryScript | `0.12.1` / ABI `11` | `verified` | 解压后的发布源码构建和安装成功，默认配置通过 42/42，最小配置通过 41/41。 |
| Render Core 源码归档 | 无 Runtime、无 JerryScript | `0.8.0` / ABI `3` | `released` | 带签名的 `v0.8.0` 源码归档；SHA-256 为 `b650866d6fcf453e6b2cb54b3a685bbe9df1be07dd9d9825060651ec1e6c4240`。 |
| Render Core 源码归档 | 无 Runtime、无 JerryScript | `0.9.0` / ABI `4` | `released` | `v0.9.0` 已签名发布；#116 将其纳入生产锁。原生和已安装 worker 的 WS147 实机记录见[ABI4 集成计划](render_core_abi4_migration_plan_zh.md)。 |
| Render Core 源码归档 | 无 Runtime、无 JerryScript | `0.10.0` / ABI `9` | `released` | `v0.10.0` 已签名发布，包含源码归档和 SHA-256 sidecar。 |
| Render Core 源码归档 | 无 Runtime、无 JerryScript | `0.6.2` / ABI `1` | `released` | 带签名 `v0.6.2` release 的确定性 `.tar.gz` 与 SHA-256 sidecar。声明为文本的成员会规范为 LF，不透明二进制成员保持原样；CI 会用等价的 CRLF/LF 源树证明 archive 字节相同，再解压、构建、运行 CTest、安装并配置 Runtime package consumer。 |
| JellyFrame App Runtime | 已安装 Render Core package | `0.9.0` / ABI `4` | `verified` | #116 至 #127 的历史生产锁。ABI4 兼容 CI 使用独立的精确锁。 |
| JellyFrame App Runtime | 已安装 Render Core package | `0.10.0` / ABI `9` | `verified (host)` | #128 纳入的历史生产锁。[主机资格验证](abi9-paired-qualification.md)覆盖记录中的 ABI9 组合的 full、cropped、off。 |
| JellyFrame App Runtime | 已安装 Render Core package | `0.12.0` / ABI `11` | `verified (host)` | [PR #143](https://github.com/xiaopaoya/JellyFrame/pull/143) 纳入的历史生产锁。已安装包通过消费者专项、桌面和脚本测试。 |
| JellyFrame App Runtime | 已安装 Render Core package | `0.12.1` / ABI `11` | `verified (host)` | 当前生产锁。使用精确生产锁的 full-contextual 包通过 41/41 项消费者测试。 |
| JellyFrame App Runtime | 已安装 Render Core package | 错误版本、ABI 或 source identity | `rejected` | 配置阶段执行精确版本、engine ABI 和 source hash 检查；package 模式不允许偷偷回退到源码 Core。 |
| App package 预检 | 生成的 Render Core capability profile | schema `1` / 所选 Core engine ABI | `verified` | `package_app.py` 会在读取资源前校验 profile schema、已知 feature ID 和依赖闭包；缺失必需能力族会拒绝 package。 |
| JellyFrame Script bridge | 源码内 Render Core | `0.6.0-dev` 源码线 | `独立验证` | 构建期选择一个脚本后端；当前实现为 JerryScript，仍是可选的 App Runtime 依赖；这不等于 package-mode scripting 已验证。 |
| App Runtime / 未来 Device OS host | `jellyframe_device_runtime_contracts` | `JFDP/1` | `verified` | 该 target 独立构建并测试 framing、typed payload 与 staging，不依赖 App Runtime 或 Render Core 的实现。其 monorepo 位置仍处于过渡期；这不是 Device OS 发布。 |
| Device Runtime / launcher | Render Core package | 由 port 选择 | `移植侧负责` | 需要 port 自己的工具链、内存 profile、panel path 和实机报告；桌面 package 证据不构成实机结论。 |
| 普通 `.jfapp` | native Render Core module | 任意 | `设计上不支持` | app package 只携带资源和声明的脚本，不能加载任意可执行 native module。 |

## 锁定的消费契约

Runtime package consumer 从 `cmake/jellyframe_dependency_lock.cmake` 读取：

```text
JELLYFRAME_RENDER_CORE_LOCKED_VERSION = 0.12.1
JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI = 11
JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH = d74b28ca238774b6f3b400a5d264b376a8c9dc02ef1e2f65c33c75305f4c254e
```

这份锁是消费者策略。Core 可以独立发布；Runtime 通过独立评审、已安装包构建和能力 profile 核对采用新版本。
已签名的 [`v0.12.1` 发布](https://github.com/xiaopaoya/JellyFrame-Render-Core/releases/tag/v0.12.1)
的源码归档 SHA-256 为 `9662b269b86239a1b93a80405dbf67466237c13c2ecab9d7e4eec2935551c521`。
归档校验值与 source manifest 分开记录；已安装的 CMake package 只能证明源码身份。

每次配置都会在复制或生成的 profile 旁写出
`generated/jellyframe_render_core_provenance.json`。它是 Runtime 或 port 构建证据
应归档的可移植记录：provider、实际 package 版本/ABI、profile 文件名、消费者锁定值和确定性
source hash/file count。已安装 package 还必须提供匹配的 source manifest；package 模式会校验、与精确 lock 比对并复制它。
该 hash 是内容身份，不替代已签名 release 或经审查的 lock。

## 证据规则

- `verified` 表示对应构建边界已有可复现的自动化测试。
- package-consumer 只有在同一 commit 的本地 CTest 和远端 CI 都通过时，才能作为发布证据。
- `移植侧负责` 表示核心契约已提供，但结果依赖开发板、面板、工具链或 RTOS，
  必须由移植侧报告。
- 没有有效证据时必须保留 `not-tested`，不能根据桌面构建改写为 `supported`。

## 拆分顺序

1. 保留 `in-tree` provider，用于 Core 与 Runtime 同步开发。
2. 在 CI 保留确定性 source archive 和安装包 consumer，并在 Runtime 和 port 构建报告中归档
   生成的 provenance 记录。
3. 持续同步独立 Core 仓库的兼容性矩阵；开发头在完成显式 Runtime lock 更新和经审查的发布
   artifact 前，只能标记为 standalone-only。
4. 只有 Device Runtime 的宿主/移植所有权契约稳定后，才将其迁入未来的 JellyFrameOS 边界。
