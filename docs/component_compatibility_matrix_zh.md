# 组件兼容性矩阵

> 最后更新：2026-08-12；适用版本：0.6.0-dev

这份矩阵记录三个计划中的产品边界之间的兼容性证据。它不同于 HTML/CSS
能力全表：能力全表描述 app 可见行为，本文件描述哪个构建产物可以消费哪个
构建产物。

## 当前矩阵

| 消费者 | 提供者 | 版本 / ABI | 状态 | 证据与限制 |
| --- | --- | --- | --- | --- |
| JellyFrame App Runtime | 源码内 `jellyframe_render_core` | 同一 checkout | `verified` | 默认桌面 Release/Debug 和非 scripting CI CTest。适合同步修改 Core 与 Runtime。 |
| JellyFrame App Runtime | Core 源码覆盖 | 本地 checkout / source profile | `本地已验证` | `JELLYFRAME_RENDER_CORE_SOURCE_DIR` 可选用另一个 Core checkout 进行跨仓库开发；仍与 package 模式互斥。 |
| Render Core standalone 测试 | 无 Runtime、无 JerryScript | `0.6.0` / ABI `1` | `verified` | 已验证独立配置、构建、CTest 和安装；package 只包含 Core target、公共头文件和能力 profile。 |
| JellyFrame App Runtime | 已安装 Render Core package | `0.6.0` / ABI `1` | `verified` | Runtime 使用 `JELLYFRAME_RENDER_CORE_PROVIDER=package`；本地 consumer CTest 为 `8/8`，`a934846` 上的 package-consumer CI 已通过。 |
| JellyFrame App Runtime | 已安装 Render Core package | 错误版本或 ABI | `rejected` | 配置阶段执行精确版本和 engine ABI 检查；package 模式不允许偷偷回退到源码 Core。 |
| App package 预检 | 生成的 Render Core capability profile | schema `1` / engine ABI `1` | `verified` | `package_app.py` 会在读取资源前校验 profile schema、已知 feature ID 和依赖闭包；缺失必需能力族会拒绝 package。 |
| JellyFrame Script bridge | 源码内 Render Core | `0.6.0-dev` 源码线 | `独立验证` | JerryScript 仍是可选的 App Runtime 依赖；这不等于 package-mode scripting 已验证。 |
| App Runtime / 未来 Device OS host | `jellyframe_device_runtime_contracts` | `JFDP/1` | `verified` | 该 target 独立构建并测试 framing 和 staging，不依赖 App Runtime 或 Render Core 的实现。源码位置仍处于过渡期；这不是 Device OS 发布。 |
| Device Runtime / launcher | Render Core package | 由 port 选择 | `移植侧负责` | 需要 port 自己的工具链、内存 profile、panel path 和实机报告；桌面 package 证据不构成实机结论。 |
| 普通 `.jfapp` | native Render Core module | 任意 | `设计上不支持` | app package 只携带资源和声明的脚本，不能加载任意可执行 native module。 |

## 锁定的消费契约

Runtime package consumer 从 `cmake/jellyframe_dependency_lock.cmake` 读取：

```text
JELLYFRAME_RENDER_CORE_LOCKED_VERSION    = 0.6.0
JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI = 1
```

这份锁是消费者策略，不表示未来所有 Render Core 构建都必须保持同一版本。
Core 可以独立演进，但 Runtime 接受新版本前，必须更新锁、运行 package-consumer
构建，并审阅能力 profile。

每次配置都会在复制或生成的 profile 旁写出
`generated/jellyframe_render_core_provenance.json`。它是 Runtime 或 port 构建证据
应归档的可移植记录：provider、实际 package 版本/ABI、profile 文件名和消费者锁定值。
其中刻意不写猜测出来的 Git hash；只有独立 Core 发布包正式提供时，该字段才有意义。

## 证据规则

- `verified` 表示对应构建边界已有可复现的自动化测试。
- package-consumer 只有在同一 commit 的本地 CTest 和远端 CI 都通过时，才能作为发布证据。
- `移植侧负责` 表示核心契约已提供，但结果依赖开发板、面板、工具链或 RTOS，
  必须由移植侧报告。
- 没有有效证据时必须保留 `not-tested`，不能根据桌面构建改写为 `supported`。

## 拆分顺序

1. 保留 `in-tree` provider，用于 Core 与 Runtime 同步开发。
2. 在 CI 保留安装包 consumer，并在 Runtime 和 port 构建报告中归档生成的 provenance
   记录。
3. 第一个独立 Core 仓库发布前，维护完整兼容性矩阵。
4. 只有 Device Runtime 的宿主/移植所有权契约稳定后，才将其迁入未来的 JellyFrameOS 边界。
