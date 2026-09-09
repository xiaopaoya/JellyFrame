# Render Core 性能观测与对比方案

> 最后更新：2026-09-09；适用版本：0.6.0-dev
> 状态：第二阶段进行中；Win32 桌面壳已提供部分阶段计时

## 1. 为什么需要这项工具

总帧耗时只能回答“这一帧慢”，不能回答慢在 DOM 更新、style、layout、layer、dirty
规划、某类 paint command、framebuffer 写入还是 panel/DMA。JellyFrame 还同时存在
桌面运行时、Render Core-only benchmark 和设备 port，若把这些时间直接相加或相互
比较，会产生错误结论。

性能工具必须把以下三类数据分开：

| 数据源 | 能回答的问题 | 不能回答的问题 |
| --- | --- | --- |
| desktop/pseudo-browser timings | Core 各阶段的相对成本、页面复杂度 | MCU 实际 FPS、DMA、panel 时间 |
| Render Core microbench | 单个 command/算法的隔离趋势 | 该 command 在 App 中的真实归因 |
| device/port telemetry | 实机 frame/present/DMA/内存成本 | Core 内部某个 DOM 元素的耗时，除非 port 提供对应 trace |

`tools/render_performance_report.py` 将三者写入同一份 source-aware report，但不混合
计算。没有逐帧 producer 时，它会明确标记为单帧或 aggregate 数据。

## 2. 当前可用方式

对 package report、设备 aggregate telemetry 和 Render Core microbench 输出执行：

```powershell
python tools\render_performance_report.py `
  --report build\app.package.report.json `
  --device-telemetry build\port.log `
  --microbench build\render-core-microbench.txt `
  --output build\render-performance.json `
  --html-output build\render-performance.html
```

报告提供：

- frame total 的 average、p50、p95、max；
- parse、style、render tree、layout、layer、flatten、paint、present 等阶段的总耗时和占比；
- dirty rect 数量、dirty 面积、frame update action/reason 和 pipeline object count；
- producer 提供时的 display command 类型/元素归因；
- 设备 aggregate telemetry 与隔离 microbench 的独立区域；
- 明确的 warning/limitation，而不是用缺失数据填零后伪造结论。

## 3. 逐帧 trace V0

桌面壳和 port 后续以 JSONL 输出 `jellyframe.render.trace.v0`。第一条可选 session
记录描述固定上下文，之后每条 frame 记录对应一次 frame planning/render/present：

```json
{"format":"jellyframe.render.trace.v0","type":"session","appId":"org.example.app","viewport":{"width":172,"height":320},"profile":"rect-172x320","runtime":"desktop"}
{"format":"jellyframe.render.trace.v0","type":"frame","frame":42,"totalUs":17300,"stagesUs":{"input":120,"script":880,"style":410,"renderTree":620,"layout":2100,"layerTree":530,"dirty":190,"paint":10600,"present":2260},"action":"repaint-existing","reason":"paint-only-dirty","dirtyRectCount":2,"dirtyAreaPercent":3,"pipeline":{"domNodes":31,"layoutBoxes":22,"layers":4,"displayCommands":18,"paintPixels":5170},"commands":[{"type":"BoxShadow","nodeId":"card-1","us":7200,"pixels":3820},{"type":"Text","nodeId":"value","us":610,"pixels":340}]}
```

Win32 桌面壳当前可在确定性捕获时生成第一版 trace：

```powershell
build\Release\jellyframe_desktop_shell.exe `
  --app tests\fixtures\apps\jelly_scroll_probe `
  --capture-frames build\trace-frames `
  --render-trace build\render-trace.jsonl `
  --frame-count 120
```

该 producer 只在显式 `--render-trace` 下启用，并且仅接受 `--capture-frames`/帧脚本模式。
它使用确定性捕获循环的墙钟时间填充 `totalUs`，并在渲染路径中记录已覆盖的
`input`、`style`、`renderTree`、`layout`、`layerTree`、`dirty`、`paint`、`present`
阶段。`script` 目前没有独立计时，因此可能缺失；`timingComplete` 必须为 `false`，
阶段之和也不保证等于 `totalUs`。这表示“当前已归因的桌面壳阶段耗时”，不表示已经完成
所有运行时、主机任务、窗口系统或设备 DMA/panel 分项。trace 最多保存 600 条 frame、总计 4 MiB、
单行 4 KiB；记录超限或写盘失败只停用 trace，不改变渲染结果。捕获结束后一次性写盘，
避免把文件 I/O 和 flush 放进每帧 render/present 路径。首个捕获帧可能只是初始化阶段已经
完成后的 `clean-cached` 记录，因此不能把它当作应用启动首帧耗时。

### 必填与约束

- `format`、`type`、`frame`、`totalUs`、`stagesUs` 必须存在；所有时间为非负整数微秒；
- `frame` 在同一 session 严格递增；重复、回退或损坏记录必须被工具报告，不能静默排序；
- `totalUs` 是 producer 测得的 frame wall time；`stagesUs` 可以存在未归因间隙，工具不得
  宣称阶段之和等于 total，除非 producer 明确给出 `timingComplete: true`；
- `nodeId` 必须是 App/DOM 的稳定公开 ID 或 producer 生成的受限 opaque ID，不得输出裸指针、
  arena 地址、文件密钥或设备物理地址；
- `commands` 是可选归因。没有 command/node 归因时，UI 必须显示“无法归因到元素”，不能
  把整层耗时错误归给第一个元素；
- trace 必须有记录数、单行字节数和 session 总大小上限；设备侧默认只保留最近窗口，
  导出到主机后再长期保存。

## 4. VS Code 逐帧工具目标

第一阶段界面应提供：

1. 按 frame 的列表，显示 total、FPS 等效值、action/reason、dirty rect 数与面积；
2. 阶段堆叠条，点击阶段显示其绝对时间、占比和是否来自 desktop/device；
3. 画布 overlay：dirty rect、paint bounds、clip bounds，切换前后帧；
4. command/node 排名，显示类型、稳定 ID、覆盖像素和耗时；
5. frame scrubber，逐帧查看“重建了什么、复用了什么、哪些区域被清除/重绘”；
6. p50/p95 与最慢帧固定显示，并允许导出原始 JSONL/HTML；
7. 缺失 trace、设备只提供 aggregate 或 command 未归因时显示来源和限制。

实时模式应采用有界环形缓冲，不阻塞 render/present，也不在 MCU 默认开启逐元素计时。
设备侧默认只发送阶段计数和窗口汇总；需要逐命令/逐元素 profiling 时，必须显式启用
profiling profile，并记录它可能改变时序的事实。

## 5. 性能测试矩阵

### Core 算法层

固定 `300x300`、`320x240`、`172x320` 三个 viewport，分别测量：

- 空页面、静态多节点页面、paint-only 更新、text/layout 更新、树结构更新；
- FillRect、Text、Image、Linear/Radial/Conic Gradient、Rounded Clip、BoxShadow；
- 1、10、100 个 dirty rect，以及 full-frame fallback；
- 1、10、100 层/command 和短中长 UTF-8 文本；
- Debug、Release、ASan/UBSan；每项报告 p50/p95/max、峰值内存和输出像素 hash。

### Runtime/桌面层

记录 script、host completion、frame planning、style/layout reuse、layer rebuild、paint、
present 和 teardown。用相同 App 做 full repaint 与 incremental repaint A/B；要求像素等效，
不能仅以平均耗时下降判定优化成功。

### Port/实机层

每帧至少记录 frame total、render/present、flush/DMA wait、dirty/full、转换像素、packed bytes、
内部 RAM 峰值、队列丢弃和错误计数。每个 workload 固定 firmware、Core/Runtime identity、
profile、SDKCONFIG、温度/电源条件和输入脚本；硬件结论只使用同一镜像的 A/B 报告。

## 6. 与主流图形库的公平比较

不能把 JellyFrame 的 CPU 软件栅格结果直接与 GPU 加速的 Skia/浏览器合成器比较。建议
分成三组：

| 组 | 对照 | 比较目的 |
| --- | --- | --- |
| CPU 2D | Cairo、SDL software renderer 或同等 CPU backend | 比较 raster primitive 的成本和像素吞吐 |
| 嵌入式 UI | LVGL 等实际目标设备 UI 路径 | 比较 dirty、内存、输入到 present 和设备端时序 |
| 浏览器级 | Chromium/Skia 等 | 只比较相同 viewport/场景的功能覆盖与桌面趋势，不作 MCU 结论 |

每个对照必须使用相同分辨率、像素格式、是否抗锯齿、是否 full/dirty、输入场景和输出
校验规则。报告至少包括：frame p50/p95、MPix/s、dirty pixels/frame、峰值内存、CPU 时间、
present/DMA 时间和视觉误差。不同库不支持的能力单独标记 `not-comparable`。

当前基于代码和已有 telemetry 可以提出、但尚不能宣称为 benchmark 事实的工作假设是：

- JellyFrame 可能在有界 CSS 子集、跨平台同一 DOM/布局语义、可裁剪 feature profile 和
  dirty/update 诊断上更容易控制；这些优势需要矩阵实验验证；
- JellyFrame 预计在 CPU 阴影、rounded coverage、大面积渐变、复杂文本 shaping 和缺少
  GPU/硬件合成的场景更慢；已有设备归因已显示 BoxShadow 是 rounded workload 的主要耗时，
  但该结论只适用于对应 fixture，不能外推全部 App；
- 若 present/DMA 占主导，继续优化 Core paint 不会改善实机帧率；若 layout/script 占主导，
  应优先减少重建或 App 更新范围，而不是改 rasterizer。

## 7. 阶段出口

### 第一阶段：已交付

- source-aware JSON/HTML report 工具；
- 单帧 pipeline report、设备 aggregate telemetry、microbench 的明确分层；
- trace V0 的输入格式和回归测试。

### 第二阶段：进行中

- Win32 shell 已在显式选项下产生 bounded frame JSONL；
- trace report 工具会报告重复、回退或非法 frame number，不静默排序或伪造帧；
- 每帧补齐阶段 timing、dirty/pipeline counters 和 frame update reason；
- VS Code 性能面板读取 trace，支持 frame scrubber 和阶段占比；
- 不提供元素耗时猜测，直到 Layer/DisplayCommand 有稳定 node attribution contract。

### 第三阶段：设备 profile

- port 以显式 profiling 配置提供阶段/窗口 trace 或 aggregate；
- 以真实 developer-image workload 复核 Core/Runtime 优化收益；
- 完成至少一个 CPU 2D 和一个嵌入式 UI 对照，才给出“快/慢”的定量结论。
