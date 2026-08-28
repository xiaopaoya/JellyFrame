# 2026-08-28 安全与鲁棒性移植侧补测要求

> 状态：待 ESP32-S3 移植侧执行
>
> 适用主线：`86f94ee6`（`master`，CI 已通过）
>
> 适用版本：`0.6.0-dev`

## 1. 目的与范围

本要求只验证主线近期修改中依赖真实 ESP-IDF、分区存储、启动加载或硬件任务
生命周期的行为。它不是对所有外部审查意见重新验收，也不替代已有的 JFDP/1、A1-2、
A2 provider 或 value-frame v4 报告。

本轮必须确认：

1. 合法且入口 HTML 大于历史 512 B 限制的已安装 App 可以启动。
2. 损坏或不可信的 rollback 记录不会替换当前 active bundle，并产生可识别的恢复诊断。
3. 触控任务退出完成前不会释放 I2C、LCD 或同步对象，重复停止/启动不会产生 UAF。
4. 上述修改与现有安装、启动、重启、恢复流程组合时没有引入新的异常。

所有结论必须以实际烧录的固件和版本化报告为依据；桌面测试、静态代码阅读或
“重新烧录后恢复”不能替代对应实机证据。

## 2. 固件与测试前置条件

移植侧必须记录以下信息：

- JellyFrame 主线 commit：必须为 `86f94ee6` 或包含该提交的明确后续 commit；
- Device OS/port commit、Provider 版本、Render Core provenance 与 ABI；
- 板卡、屏幕、触控控制器、CPU/PSRAM、分辨率和亮度配置；
- 分区表、bundle/staging/registry 上限、文件系统或 raw partition 布局；
- 完整 `sdkconfig`、构建 profile、ELF/map hash、build/flash log；
- 测试用 `.jfapp` 的 SHA-256、manifest identity、version name/code 和入口路径。

每组测试均需执行 clean configure、clean build、flash、硬复位和正式采集。不得沿用
旧 build 目录中未确认的 `sdkconfig`、生成文件或组件缓存。

至少准备三份 fixture：

- **large-entry-A**：合法、可启动，入口 HTML 的实际资源长度大于 512 B，且含有可观察
  的唯一 launch marker；
- **rollback-B**：与 A 使用相同 app id 但版本更高，含不同的 launch marker；
- **corrupt-rollback**：由已提交的 rollback bundle 或其 descriptor/CRC/summary 中至少
  一项受控损坏产生，不能通过正常 bundle 校验。

fixture 必须通过主线 `.jfapp` 检查器；禁止使用 port 私有格式、固定 HTML shortcut 或
仅改变 native 状态的伪造 App。

## 3. 强制测试矩阵

### R1：大入口资源启动

1. 通过真实 Provider/JFDP 安装 `large-entry-A` 并完成 commit。
2. 在不重新烧录固件的情况下执行 `list`、`launch`，记录启动 marker。
3. 硬复位后再次 `list`、`launch`，确认仍能读取并启动同一 bundle。
4. 至少使用一个入口 HTML 大于 512 B、另一个明显更大的合法入口（建议 2 KiB 以上），
   以排除只恰好跨过边界的偶然结果。

通过条件：

- 两个 fixture 均完成安装、验证、持久化和启动；
- 重启后 app id、version、entry 和 launch marker 正确；
- 无 entry read failure、截断、空白页面、乱码、panic、watchdog、reset 或 storage error；
- 不得通过缩小入口文件或重新烧录规避原始场景。

### R2：rollback 完整性与 active 保留

1. 安装并启动 A。
2. 安装并启动更高版本 B，使 A 成为 rollback target。
3. 在 test-only storage 注入点损坏 rollback bundle 或其 CRC/summary/identity，不能修改
   当前 active B 的数据。
4. 请求 rollback，并记录 typed result、recovery diagnostic、registry generation 和
   active version。
5. 硬复位，重新执行 `list`、`launch` 与 `recovery`。
6. 清除受控损坏后，重新验证正常 rollback 一次；若产品策略不允许继续使用损坏的 target，
   必须明确返回 `not-found`/`invalid-bundle` 等稳定失败，而不是静默成功。

通过条件：

- 损坏 rollback 始终被拒绝，不能被写入 active；
- rollback 失败前后 B 的 bundle、version、identity 和可启动性保持不变；
- reboot 后 registry 不为空、不 torn、不指向 partial bundle；
- `recovery` 或 operation result 能区分校验失败与传输失败；
- 正常 rollback 仍能恢复 A，或按明确策略给出 unchanged-state 结果；
- 全过程无重新烧录、MCU reset loop、watchdog、panic、DMA/SPI/panel 错误。

### R3：触控任务 teardown 与重复生命周期

在真实启用触控的构建中执行：

1. 启动带触控 UI 的 App，确认至少产生一次 down/move/up 或等价的有效触控事件。
2. 停止 App/设备显示任务，等待正常 teardown 完成。
3. 依次重复启动、触控、停止至少 20 轮；其中至少 5 轮在触控仍处于按下或刚产生事件
   后立即停止。
4. 若 port 提供 I2C read timeout 或 task-stop failpoint，再额外执行一次受控超时；否则
   必须在报告中说明该 failpoint 不可用。
5. 重新进入 App，确认触控设备和显示依赖仍可正常初始化；最后执行硬复位并检查日志。

通过条件：

- 每轮都能观察到 touch task 自行退出后才释放 I2C、LCD 和同步对象；
- 无 use-after-free、非法访问、heap corruption、double delete、panic、watchdog 或 reset；
- 无 I2C/LCD handle 错误、触控任务永久残留、队列泄漏或启动后无响应；
- 失败或超时路径必须是有界的 typed/logged failure，不能以释放仍被任务使用的对象作为兜底；
- 结束后资源可再次创建，且内存最低水位和任务数量没有逐轮恶化。

### R4：组合回归

在 R1 至 R3 单项通过后，执行至少 30 轮以下序列：

`install large-entry-A -> launch -> touch -> stop -> reboot -> list -> launch -> rollback`

若某轮 rollback target 已被受控损坏，则该轮应验证稳定拒绝和 active 保持；不得把损坏
target 当作成功 rollback。每轮记录 registry generation、active version、staging 数量、
任务创建/退出计数和错误计数。

通过条件：

- 30/30 完成或得到文档化的稳定拒绝；
- generation 单调递增且无重复发布、回退或空 registry；
- 无 panic、watchdog、brownout、reset loop、DMA/SPI/panel/I2C 错误；
- staging 和 touch task 资源不随轮次无界增长；
- 任意失败后仍可回到 protected launcher 或上一次已提交 App。

## 4. 端侧无需重复的项目

以下项目已有主线或 host/provider 定向回归，不要求移植侧仅为本轮重新实现或重复证明：

- JS service kind、session/input handle 和 supervisor lease 校验；
- JerryScript watchdog 缺失时的 fail-closed 初始化；
- surface 字节数 checked multiply；
- Render Core CSS 百分比、aspect-ratio 算术溢出；
- Provider stdout/stderr 增量读取和 256 KiB 上限；
- `package --debug-dir`、history export 路径保护和 package 资源预算硬错误；
- C++/Python JFDP `receivedBytes <= expectedBytes` codec 校验；
- Python reference device 的 fsync/掉电尾部恢复；
- app 私有数据目录的无损 ID 编码。

若移植侧修改了上述代码或在端侧实现了同等逻辑，可在报告中附带回归结果，但不得
用端侧结果覆盖主线测试结论。

## 5. 证据目录与报告格式

建议目录：

`test_artifacts/security-robustness-<board>-<commit>-<date>/`

至少包含：

- `report.md`：环境、fixture、逐项步骤、实际结果、人工观察和最终 verdict；
- `summary.json`：每个 R1/R2/R3/R4 case 的 pass/fail、计数器和错误字段；
- 原始与清理后的串口日志；
- Provider stdout/stderr、CLI 命令及 JFDP capture（如适用）；
- build/config/flash log、ELF/map 或 hash；
- fixture、manifest、image identity 和 SHA-256；
- 失败时的最小复现、未裁剪日志和故障发生前后的 registry/storage 摘要。

`summary.json` 至少应区分：

```json
{
  "mainlineCommit": "86f94ee6",
  "largeEntryLaunch": {"status": "pass", "cases": 0},
  "rollbackIntegrity": {"status": "pass", "corruptRejected": 0, "activePreserved": 0},
  "touchTeardown": {"status": "pass", "cycles": 0, "taskExitTimeouts": 0},
  "combinedLifecycle": {"status": "pass", "cycles": 0},
  "errors": {"panic": 0, "watchdog": 0, "reset": 0, "i2c": 0, "lcd": 0, "dma": 0, "spi": 0, "panel": 0}
}
```

实际字段可以扩展，但不能只提供一个总的 `passed: true`。每个失败、中断、拒绝和
recovery 都要能关联到 case、transaction/generation（如适用）和稳定 reason。

## 6. 判定与后续动作

- R1、R2、R3 是独立强制项，任一失败则本轮安全/鲁棒性移植补测为 `partial`；
- R4 只有在前三项完成后才有意义，不能用 R4 的部分成功掩盖单项失败；
- 未执行的 failpoint 必须标记 `not-tested`，不能写成通过；
- 只要出现 active 被损坏替换、无法恢复的 registry、UAF/heap corruption、watchdog 或
  reset loop，立即停止扩大测试并提交最小复现；
- 证据通过后，主线维护者再更新 `project_docs/security_robustness_review_20260828_zh.md`
  的“验证边界”、`roadmap_zh.md` 和 `todo_zh.md`，关闭相应移植侧待办。

本要求不授权启用 retained replay、framebuffer reuse、tile/scanline renderer、Canvas
host binding 或新的 CSS 能力；这些仍按各自 RFC 和验收文档处理。
