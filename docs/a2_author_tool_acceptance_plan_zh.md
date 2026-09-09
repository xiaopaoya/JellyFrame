# A2 作者工具与设备流程验收方案

> 最后更新：2026-09-09；适用版本：0.6.0-dev
> 状态：执行方案；A2 当前仍为 partial

本文是 A2 的总验收入口，统一串联 VS Code/CLI 作者流程、WS147 Developer Image、
provider、已安装 App 的运行与输入证据。它不重复实现 provider lifecycle；已有的
provider、JFDP 和 storage 报告只能作为前置证据，不能替代本方案要求的作者工具和
真实 App 验收。

## 1. 目标与边界

A2 通过后，一个不安装 ESP-IDF、不打开 JellyFrame 源码仓库的 App 作者，应能在干净
主机上完成：

```text
安装扩展/SDK -> 创建 blank App -> check -> package -> 发现设备 -> 安装
-> 启动 -> 查看实时日志 -> 更新 -> rollback -> stop -> remove
```

本轮不把完整浏览器兼容性、Canvas host binding、全屏渐变 30 FPS 或第二脚本引擎
作为 A2 通过条件；这些能力必须按能力表明确标记为未支持或未测试。A2 也不允许
通过固定 fixture、串口文本解析、桌面壳回放或改写 manifest 来替代真实设备证据。

## 2. 固定输入

每次正式验收必须固定并记录以下版本，不允许混用旧包：

| 项目 | 当前验收输入 |
| --- | --- |
| Developer Image | `0.6.2-ws147.1`，`rect-172x320` |
| Render Core | `0.6.2`，ABI `1`，source identity 与 manifest 一致 |
| Provider | `jellyframe-device@0.1.1-dev`，含 lifecycle capability |
| App SDK | `app-sdk-v0.6.0-dev.2`，普通与 scripting 桌面运行时均可用 |
| 目标板 | WS147 / ESP32-S3；一次测试只绑定一个明确 endpoint |

归档 firmware、factory/recovery image、manifest、provider ZIP、SDK/VSIX 的 SHA-256，
以及完整的插件版本、操作系统、Python/Node 版本。身份不匹配时立即标记 `blocked`，
不得通过选择旧 manifest 或降级 provider 继续测试。

## 3. 测试分层

### A2-0：主机前置与干净环境

在没有 JellyFrame 源码、ESP-IDF、旧 provider 配置和旧构建目录的用户目录中：

1. 安装 VSIX，安装或下载匹配 SDK；SDK 目录不位于框架源码目录。
2. 用扩展的“创建 App”从 `blank` 创建到独立目录，并能自动打开该工作区。
3. `blank` 只包含最小 HTML/CSS/JS 和 `.app.json`；schema 能从 SDK/扩展实际可用
   的本地路径解析，不依赖不存在的公网 URL。
4. 在未配置 SDK/provider 时，功能入口给出可操作的配置引导；错误显示阶段、对象、
   稳定 reason 和下一步动作，不只显示 `0/1/2`。

**通过标准：**新用户不需要手工修改 CMake、相对路径、Python 环境或源码目录；扩展
   能识别 SDK、桌面构建和 provider，缺失项能准确指出并可回到配置流程。

### A2-1：只读设备发现

按 `discover -> info -> list` 执行一次，再连接第二块板重复执行，确认设备选择器和
   状态栏始终显示当前 endpoint 的短标识、端口、profile 和连接状态。

必须检查：

- `discover` 返回稳定 opaque `endpointId`，不泄露端口路径、flash 地址或密钥；
- `info.identity` 与 manifest 的 image version、Runtime/Core version、ABI、profile、
  viewport、feature family 完全匹配；
- `list` 显示同一 endpoint 的 registry generation、App ID、版本、状态和 rollback 摘要；
- 多设备时可明确选择目标，不得静默操作最近发现的设备；
- provider stdout 只有协议 JSON/JSONL，日志进入 stderr 或 VS Code Output。

**通过标准：**三项命令及插件操作均成功；当前目标设备可追溯；不存在 identity
   mismatch、endpoint 串线、重复 response、未解释错误或 transport 错误。

### A2-2：桌面预检与打包

在独立 blank App 上依次执行 `check`、`preview`、`package`，再使用一个 scripting
   App 重复。至少检查：声明尺寸、入口文件、资源路径、字体、script profile、总包大小
   和目标能力。

**通过标准：**

- 合法包可生成 `.jfapp`，manifest summary 与包内容一致；
- 不支持的 CSS/JS、缺失字体、超尺寸/超预算资源在 check/package 阶段明确提示，
  不生成设备必然拒绝的包；
- preview 与设备声明 viewport 一致；已知不支持项不被静默 fallback 成“已支持”；
- preview、package 和 install 使用同一份输入及可追溯 hash。

### A2-3：安装、启动与实时日志

用一个非脚本 App 和一个 `runtime.script: "classic"` App 各执行一次：

1. install，记录 progress、commit、registry generation 和最终 result；
2. launch，确认进入真实入口资源，而非默认静态页；
3. 观察实时 app-scoped logs，确认 app ID、generation、timestamp、level；
4. stop，等待 worker/UI teardown 完成，再检查旧 frame/input 不再到达。

**通过标准：**install 只在原子 publish 后报告成功；launch/stop 均有明确完成状态；
   日志有界且可定位；无旧 session 输入、旧 frame present、worker 残留、panic、
   watchdog、reset、brownout、DMA/SPI/panel 错误。

### A2-4：更新、回滚、删除与重连恢复

对同一 App 完成以下矩阵，每个关键步骤至少在一次断开重连或硬复位后继续：

| 用例 | 操作 | 必须保持的性质 |
| --- | --- | --- |
| 正常更新 | v1 -> v2 | generation 增长；v2 可启动 |
| 回滚 | v2 -> v1 | v1 完整可启动；registry 无半发布 |
| 删除 | stop -> remove | bundle 删除；按选项处理私有数据 |
| 中途取消 | install chunk 期间 abort | 终态为 confirmed cancelled；旧已提交版本仍可用 |
| 断线恢复 | 传输/重连/复位 | 不重复 commit，不重复归属 response |
| 失败恢复 | load/runtime fatal | 回到受保护 launcher；旧 session 全部失效 |

**通过标准：**每个失败或中断都保留此前 committed state；registry generation 严格
   单调且无 partial publish；无需重新刷写即可恢复到 launcher 或旧版本；没有异常
   reset loop、数据损坏或跨 App 操作。

### A2-5：真实 App panel/input 验收

安装专用交互 fixture，至少包含按钮、range slider、静态文本和背景。fixture 必须
   由输入 handler 改变可见状态，不能用 timer 伪造结果。

执行：

1. 按钮点击至少 3 次，每次只增加一次计数；
2. slider 低位到高位、再反向拖动，检查起点/中点/终点以及拖动中间状态；
3. stop/relaunch 后重复一次点击和一次拖动；
4. 保存同一运行段的 `posted`、`worker_seq`、`mutation_seq`、`published_seq`、
   `accepted_seq`、`presents_failed` 及 app-scoped logs；
5. 记录 panel 目检：文字、圆角、尺寸、局部刷新、残影、闪烁、裁切和实际 viewport。

**通过标准：**

- 每次 click 与 drag 都产生对应的真实 JS/DOM 可见变化；drag 使用带有按钮状态的
  value packet，不能只在 up 时更新；
- 输入、mutation、frame publish、accepted present 的序列最终推进且无永久停滞；
- 正常段 `rejected=0`、`unsupported=0`、`queue_dropped=0`、`presents_failed=0`；
- 没有 watchdog、panic、reset、brownout、DMA/SPI/panel 错误或 session 串扰；
- panel 目检与 preview 的尺寸、对齐和关键状态一致。视觉差异必须附截图和归因，
  不能用“看起来正常”覆盖计数异常。

### A2-6：失败路径与可诊断性

在无板卡 host fixture 和一次真实设备流程中分别覆盖：SDK 缺失、provider 未配置、
provider 不可执行、无设备、identity mismatch、transport unavailable、storage full、
malformed App、超预算 App、安装中断、取消未确认和运行时 fatal。

**通过标准：**每个失败都同时给出：

- `stage`：SDK、package、manifest、provider、transport、registry、runtime 或 port；
- 稳定 `reason` 与面向用户的中文说明；
- 是否已改变设备状态、是否可重试、推荐动作；
- 原始 stderr/Output 和 request ID，且不泄露路径、密钥或内部指针。

“JellyFrame command failed with code 1/2”只能作为进程退出摘要，不能是唯一用户
可见错误。未知失败必须标为 `internal-error` 并附 stage/request ID，不能伪装成成功。

## 4. 运行次数与数据要求

- A2-0 至 A2-3：每个核心流程至少冷启动 2 次，第二次必须使用全新工作区。
- A2-4：正常 update/rollback/remove 各 3 次；取消、断线、load failure 各 1 次。
- A2-5：按钮 3 次、slider 往返 2 次、stop/relaunch 1 次；可选压力段至少 20 次。
- 多设备：至少 2 个同时可见 endpoint，验证选择与操作归属。
- 所有实机操作必须使用同一 manifest/provider/image 组合；Provider 独占端点，不能
  并行打开原始 serial monitor。

每次归档目录至少包含：`report.md`、`summary.json`、版本与 hash、操作时间线、
provider stdout/stderr、VS Code Output、设备日志、App 源码与 `.jfapp` hash、
preview/设备截图及原始计数。`summary.json` 必须能区分 `pass`、`partial`、`blocked`
和 `fail`，不能只写一个总布尔值。

## 5. 总通过判定

### PASS

仅当 A2-0 至 A2-6 的适用项全部满足，且：

1. 干净作者机完整流程可重复完成；
2. identity、endpoint、registry 和 App session 全程可追溯；
3. 真实非脚本/脚本 App 的 launch、输入、present、日志和 teardown 均有结构化证据；
4. 所有受控失败均可定位并恢复，未发生未解释的系统级异常；
5. 归档完整，报告中的版本、hash、命令和结果可以由第三方复核。

### PARTIAL

核心流程通过，但缺少某一类正式证据，例如只有人工“响应正常”而没有 input-to-present
   计数，或 provider lifecycle 通过但干净作者机流程未完成。PARTIAL 不得用于宣布 A2
   已关闭或开始外部硬件试用。

### BLOCKED / FAIL

版本/身份不匹配、无法执行关键流程、真实 App 无法输入或呈现、出现未解释 reset/panic/
watchdog/registry 损坏、旧 session 串扰，或任何 P0 安全/数据完整性问题，均为
BLOCKED 或 FAIL。必须保留原始证据，修复后从受影响阶段重新执行，不能用后续成功步骤
覆盖失败。

## 6. A2 关闭后的动作

A2 PASS 后才将路线图中的 A2 标记为关闭，并允许 A3 进入有限外部试用。若仅完成
provider lifecycle 或桌面 preview，继续保持 `partial`；下一步优先补齐缺失的作者机
与 panel/input 证据，而不是扩张新的 Core CSS 能力。
