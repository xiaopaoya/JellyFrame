# 版本规则

> 最后更新：2026-08-15；适用版本：0.6.0-dev

JellyFrame Engine 使用轻量语义化版本规则：

```text
MAJOR.MINOR.PATCH[-stage]
```

## 规则

- `1.0` 前，`0.y.0` 表示新的活跃契约线，可以有意包含不兼容的 runtime 或公开 API
  变更。
- `1.0` 前，`0.y.z` 通常是定向修复；但当移除错误或有害的预发布契约能减少重复所有权或
  维护成本时，也可以包含该移除。
- `-dev`：可变的活跃开发状态，不是兼容性目标。
- 从 `1.0` 开始，`MAJOR.MINOR.PATCH` 遵循稳定公开契约政策：不兼容变更需要 major
  版本，并在用户需要时作出明确迁移决定。

当前“架构清晰优先于预发布产物兼容”的规则见
`pre_1_0_evolution_policy_zh.md`。

Render Core package 在仓库版本之外增加一个兼容性维度，但不改变仓库的版本规则：

- package 版本跟随 Render Core 的发布版本。
- engine ABI 使用独立整数表示；只有导出的 Core target 契约不兼容时才递增。
- JellyFrame Runtime 在 `cmake/jellyframe_dependency_lock.cmake` 中同时锁定
  package 版本和 ABI；配置阶段会拒绝不匹配的 package。
- Device OS、设备协议和 port 版本属于独立契约，不能从 Render Core package
  版本推断。

## 产品版本流

计划中的仓库不共用一个版本号：

| 版本流 | 示例 | 契约 owner |
| --- | --- | --- |
| Render Core | `0.6.x-dev` | Core API/ABI、feature profile schema 和渲染行为 |
| JellyFrame Runtime | `0.6.x-dev` | Japp 格式、App Runtime 和 JerryScript binding |
| JellyFrame Device OS | `0.1.x-dev` | launcher、registry、设备生命周期、镜像和 port |
| JFDP | `JFDP/1` | 设备控制 framing 和 result-code 兼容性 |

当前 manifest schema 同时要求 `runtime.minJellyFrame` 和
`runtime.minRenderCore`。`1.0` 前它们都是精确的活跃开发线 gate：前者由 `VERSION`
推导，后者由 Runtime 锁定的 Render Core package 推导。schema、packer、registry
summary 与原生 Runtime parser 会同步执行这对校验。

## 发布期望

- 当前源码版本记录在 `VERSION`。
- 面向用户可见的变更记录在 `CHANGELOG.md` 和 `CHANGELOG_zh.md`。
- Release CTest 必须保持有效：测试二进制会显式取消 `NDEBUG`，若 `assert(...)` 被关闭则构建失败。CI 也会运行 Debug CTest。Linux CI 额外以 AddressSanitizer 和 UndefinedBehaviorSanitizer 运行非 scripting 的核心/工具覆盖；可选 JerryScript bridge 仍需兼容 sanitizer 工具链，才能关闭其独立的 sanitizer 关卡。
  本地 Windows Clang sanitizer 使用 `RelWithDebInfo`：Debug CRT 与动态 ASan runtime 不兼容；CMake 会把该 runtime 部署到 sanitizer 测试二进制旁。
- 公开文档提供英文和中文版本。中文文件使用 `_zh` 后缀。
- 公开 Markdown 文档顶部带一行轻量新鲜度信息：
  `最后更新：YYYY-MM-DD；适用版本：VERSION`。当文档的契约、示例或操作说明变化时更新这行。
  新 `-dev` 周期中，未发生实质变化的文档可以保留上一稳定版版本号，直到下一次实质审阅；日期和版本会明确显示这一状态。
  自动生成的支持表使用 `审计快照` 行；CTest 会拒绝缺少格式正确新鲜度/版本标记的一方 Markdown。
- 授权条款以 `LICENSE`、`COMMERCIAL.md` 和 README 的授权说明为准。
- 早期版本预计保持小步、里程碑式发布。

## 早期版本映射

- `0.1.x`：静态 HTML/CSS 文档核心。
- `0.2.x`：framebuffer renderer 和输入路由。
- `0.3.x`：可穿戴 app runtime 开发线，包括可选 JerryScript、DOM mutation APIs、
  packaging、文本/字体工作流和嵌入式内存优化。
- `0.4.x`：面向可安装 package app 的 runtime 稳定化，包括管线 diagnostics、
  responsive target report、有界动画、宿主服务策略、font-family 选择和 Win32 验证工具。
- `0.5.x`：设备可用性阶段，包括 storage lifecycle 接入、retained rendering 分片、
  产品级 image codec adapter、system shell recovery 和更多实机验证。
- `0.6.x`：外部开发者试用阶段。只有完成 0.5 的设备可用性、诊断与宿主契约关闭条件后才进入；
  重点是试用反馈、分发语义和目标设备证据，不表示转向完整浏览器兼容。
- `0.7.x` 至 `0.9.x`：收敛独立的 Core/Runtime/Device OS 边界，移除剩余过渡所有权并
  准备公开契约。只要有文档和完整测试，破坏性清理仍被允许。
- `1.0`：首个稳定公开契约发布。manifest、capability、target gate、诊断码和
  host-service 错误语义从此需要兼容性与审慎迁移要求。
