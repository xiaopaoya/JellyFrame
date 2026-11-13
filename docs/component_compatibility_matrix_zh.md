# 组件兼容性矩阵

> 最后更新：2026-11-13；适用版本：0.6.0-dev；Core 0.9.0-dev 验收 / Runtime 0.8.0 生产锁

这份矩阵记录三个计划中的产品边界之间的兼容性证据。它不同于 HTML/CSS
能力全表：能力全表描述 app 可见行为，本文件描述哪个构建产物可以消费哪个
构建产物。

## 当前矩阵

| 消费者 | 提供者 | 版本 / ABI | 状态 | 证据与限制 |
| --- | --- | --- | --- | --- |
| JellyFrame App Runtime | 源码内 `jellyframe_render_core` | 同一 checkout | `verified` | 默认桌面 Release/Debug 和非 scripting CI CTest。适合同步修改 Core 与 Runtime。 |
| JellyFrame App Runtime | 解压后的 Core 源码归档 | `0.6.2` source profile | `verified` | `JELLYFRAME_RENDER_CORE_SOURCE_DIR` 会为独立 Runtime 构建选用解压后的归档，并运行 App Runtime CTest；仍与 package 模式互斥。 |
| Render Core standalone 测试 | 无 Runtime、无 JerryScript | `0.6.2` / ABI `1` | `verified` | 已验证独立配置、构建、CTest 和安装；package 只包含 Core target、公共头文件和能力 profile。 |
| Render Core 源码归档 | 无 Runtime、无 JerryScript | `0.8.0` / ABI `3` | `released` | 带签名的 `v0.8.0` 源码归档；SHA-256 为 `b650866d6fcf453e6b2cb54b3a685bbe9df1be07dd9d9825060651ec1e6c4240`。 |
| 独立 Core `master` | 无 Runtime、无 JerryScript | `0.9.0-dev` / ABI `4` / source `5cf5b2e2…c714`（111 个 manifest 条目） | `qualification track` | 当前 master `076b8b0f`；Core standalone 与 Runtime ABI4 full/cropped consumers 已通过，包括 worker frame v9/v10 generated-text/replay 和 shared-inline `counter(list-num)`。目前没有签名 ABI4 package；已检查的工作区未找到 Device OS 镜像验收 artifact。 |
| Render Core 源码归档 | 无 Runtime、无 JerryScript | `0.6.2` / ABI `1` | `released` | 带签名 `v0.6.2` release 的确定性 `.tar.gz` 与 SHA-256 sidecar。声明为文本的成员会规范为 LF，不透明二进制成员保持原样；CI 会用等价的 CRLF/LF 源树证明 archive 字节相同，再解压、构建、运行 CTest、安装并配置 Runtime package consumer。 |
| JellyFrame App Runtime | 已安装 Render Core package | `0.8.0` / ABI `3` / 规范化的锁定 source manifest identity | `verified` | Runtime 使用 `JELLYFRAME_RENDER_CORE_PROVIDER=package`；会将 package manifest 与精确锁定的 source hash 比对，再复制到构建 provenance。identity 会规范化文本换行，因此同一源码的 Windows 与 Unix checkout 得到相同结果。 |
| JellyFrame App Runtime | 已安装 Render Core package | 错误版本、ABI 或 source identity | `rejected` | 配置阶段执行精确版本、engine ABI 和 source hash 检查；package 模式不允许偷偷回退到源码 Core。 |
| App package 预检 | 生成的 Render Core capability profile | schema `1` / 所选 Core engine ABI | `verified` | `package_app.py` 会在读取资源前校验 profile schema、已知 feature ID 和依赖闭包；缺失必需能力族会拒绝 package。 |
| JellyFrame Script bridge | 源码内 Render Core | `0.6.0-dev` 源码线 | `独立验证` | 构建期选择一个脚本后端；当前实现为 JerryScript，仍是可选的 App Runtime 依赖；这不等于 package-mode scripting 已验证。 |
| App Runtime / 未来 Device OS host | `jellyframe_device_runtime_contracts` | `JFDP/1` | `verified` | 该 target 独立构建并测试 framing、typed payload 与 staging，不依赖 App Runtime 或 Render Core 的实现。其 monorepo 位置仍处于过渡期；这不是 Device OS 发布。 |
| Device Runtime / launcher | Render Core package | 由 port 选择 | `移植侧负责` | 需要 port 自己的工具链、内存 profile、panel path 和实机报告；桌面 package 证据不构成实机结论。 |
| 普通 `.jfapp` | native Render Core module | 任意 | `设计上不支持` | app package 只携带资源和声明的脚本，不能加载任意可执行 native module。 |

## 锁定的消费契约

Runtime package consumer 从 `cmake/jellyframe_dependency_lock.cmake` 读取：

```text
JELLYFRAME_RENDER_CORE_LOCKED_VERSION    = 0.8.0
JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI = 3
JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH = fa5914ce1559da5b12522fa28775287a535543d1be64da19a985e3d3f84cd795
```

这份锁是消费者策略，不表示未来所有 Render Core 构建都必须保持同一版本。

首个独立 release 是 `v0.6.0`；独立开发头为 `0.9.0-dev` / ABI 4，仍处于验收轨道；Runtime package 模式已接纳带签名的
[`v0.8.0`](https://github.com/xiaopaoya/JellyFrame-Render-Core/releases/tag/v0.8.0)。
其确定性 source archive 的 SHA-256 为
`b650866d6fcf453e6b2cb54b3a685bbe9df1be07dd9d9825060651ec1e6c4240`。
该 release-artifact identity 与 source manifest 分开记录，因为已安装的 CMake package
无法证明自己来自哪一份 archive。
Core 可以独立演进，但 Runtime 接受新版本前，必须更新锁、运行 package-consumer
构建，并审阅能力 profile。

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
