# Render Core 性能观测与对比方案

> 最后更新：2026-09-17；适用版本：0.6.0-dev
> 状态：第二阶段已交付；Win32 capture 已提供有界 command/owner 归因、跨帧聚合、阶段 span 时间线和 mutation source 关联

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
  --microbench-baseline build\render-core-microbench-baseline.txt `
  --output build\render-performance.json `
  --html-output build\render-performance.html
```

报告提供：

- frame total 的 average、p50、p95、max；
- parse、style、render tree、layout、layer、flatten、paint、present 等阶段的总耗时和占比；
- dirty rect 数量、dirty 面积、frame update action/reason 和 pipeline object count；
- trace 提供时，逐帧报告会保留有界的 `captureFile`、`dirtyRects`、`stageSpans` 和
  `commandSpans`；HTML 帧表会标出这些实际观测数据的数量，不把缺失 span 推断成阶段时间线；
- producer 提供时的 display command 类型与受限 owner 归因排行；
- trace source-aware JSON/HTML 归档还会在 summary 中按 `kind + owner` 汇总 mutation source 的关联命令
  耗时、dirty evidence 耗时、命中数和覆盖帧数；该汇总保留相关性边界，不把同 owner 或空间重叠宣称为
  mutation 根因；
- 设备 aggregate telemetry 与隔离 microbench 的独立区域；
- Render Trace 查看器提供跨帧的 command/stage/owner 调用数、累计耗时和单次调用 p95；新 trace
  还会按 `type + owner + stage` 聚合真实 `commandSpans`，显示 raster invocation 的调用数、累计耗时、p95
  和候选像素；
- 新 trace 还会按 `type + owner` 聚合命令与 dirty rect 的空间重叠证据，显示命中次数、累计重绘耗时、
  p95、命中 dirty rect 数和重叠像素；这用于定位反复参与局部重绘的元素，不代表其触发了 DOM mutation；
- HTML 报告会展示普通 microbench 的平均耗时，以及统计型 probe 的 p50/p95、display command 数和峰值 surface
  字节数；孤立 probe 不被解释为 App 元素耗时或设备性能；
- 传入 `--microbench-baseline` 后，报告会按同名且同形状的 probe 展示当前值相对基线的变化百分比；
  不匹配的 probe 会跳过，不会被强行比较；
- 当前帧视图额外显示最耗时阶段、最耗时绘制命令和累计耗时最高的归因对象；归因缺失时保留
  `unattributed`，不会将未归因的墙钟时间错误分配给任意元素；
- 当前帧视图提供可点击的阶段构成条，显示各阶段累计耗时、frame wall time 占比和 producer
  声明的 runtime 来源。该构成条按 producer 字段顺序排列，但不伪装成真实开始/结束 span；
  `stagesUs` 尚不足以表达阶段间空隙或嵌套关系；
- 当前帧视图还会与上一条有效 frame 记录对比总耗时、dirty 数量/面积、阶段耗时和命令/owner
  耗时增量。命令比较只在相邻帧使用相同数据源时启用；真实 span 与旧聚合不会混比；
- mutation source 表会按安全 owner 直接显示同帧关联命令的耗时/调用数，以及与 source dirty rect
  编号相交的 dirty evidence 耗时/命中数；`owner-command`、`owner-dirty` 和 `source-only` 只表示
  关联证据强度，不把空间重叠或同 owner 自动解释为 DOM mutation 因果；
- 聚合区提供有界帧耗时趋势和“仅显示异常帧”筛选。异常以当前 trace 的总耗时、paint 耗时或 dirty
  面积 p95 为阈值，仅标记严格超过阈值的帧；它用于快速筛选，不替代基于 workload 的性能结论；
- 选中异常帧后显示异常归因详情：列出触发阈值及当前值、最耗时阶段、最耗时命令、dirty evidence
  中累计耗时最高的 owner，以及相对上一帧增长最大的命令；这些是性能相关性和空间重叠证据，
  不是 DOM mutation 的确定根因；命令、dirty 或上一帧数据缺失/截断时必须显示限制；
- 查看器固定显示有效 frame 的总耗时 p50、p95 和最大值，作为当前 trace 的桌面基线；这些数值
  不代表设备 FPS 或 DMA/panel 时序；
- 截断帧、无效计时与 `unattributed` 单独计数，聚合结果明确标记为已记录样本的下界；
- 明确的 warning/limitation，而不是用缺失数据填零后伪造结论。

## 3. 逐帧 trace V0

桌面壳和 port 后续以 JSONL 输出 `jellyframe.render.trace.v0`。第一条可选 session
记录描述固定上下文，之后每条 frame 记录对应一次 frame planning/render/present：

```json
{"format":"jellyframe.render.trace.v0","type":"session","appId":"org.example.app","viewport":{"width":172,"height":320},"profile":"rect-172x320","runtime":"desktop"}
{"format":"jellyframe.render.trace.v0","type":"frame","frame":42,"totalUs":17300,"stagesUs":{"input":120,"script":880,"style":410,"renderTree":620,"layout":2100,"layerTree":530,"dirty":190,"paint":10600,"present":2260},"action":"repaint-existing","reason":"paint-only-dirty","dirtyRectCount":2,"dirtyAreaPercent":3,"pipeline":{"domNodes":31,"layoutBoxes":22,"layers":4,"displayCommands":18,"paintPixels":5170},"commands":[{"type":"BoxShadow","owner":"id:card-1","us":7200,"pixels":3820,"samples":1},{"type":"Text","owner":"n7","us":610,"pixels":340,"samples":2}]}
```

当 producer 能提供真实 span 时，frame 还会附带可选的 `stageSpans`，例如：

```json
"stageSpans":[{"name":"input","startUs":0,"durationUs":120},{"name":"style","startUs":140,"durationUs":410},{"name":"paint","startUs":3260,"durationUs":10600}]
```

`stagesUs` 继续作为兼容的累计聚合字段；查看器只有在存在 `stageSpans` 时才绘制实际位置和
阶段间隙，否则仅显示累计阶段构成。

桌面 capture 还可提供有界的 `commandSpans`，每项包含 `type`、`owner`、相对于 frame
 trace 起点的 `startUs`、`durationUs`、候选 `pixels` 和最终 raster clip 的 `rect`。它只覆盖实际 raster invocation；
 rounded composite、transform 和宿主绘制等没有独立命令时钟的工作仍保持未归因。

Win32 桌面壳当前可在确定性捕获时生成第一版 trace。启用
`--capture-frames` 时，每条 frame 记录还会带有相对截图文件名
`captureFile`（例如 `frame_000.bmp`），使查看器能够把指标与实际画面
绑定。每条 frame 同时带可选的 `dirtyRects`：这是最终 dirty region 的最多 32 个
整数 `{x,y,width,height}` 矩形；超过上限时以 `dirtyRectsTruncated: true` 显式标记。
它用于解释本帧重绘范围，不含指针、设备地址或内部节点身份。旧 trace 没有这些字段时查看器
仍可正常打开。旧 trace 没有 `captureFile` 时，查看器会在 trace 同目录按帧号查找
`frame_*.bmp`、`.ppm` 或 `.png`；找不到时明确显示截图缺失，不将其当作
空白画面：

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
完成后的 `clean-cached` 记录，因此不能把它当作应用启动首帧耗时。显式 trace capture 的第 0 帧
会请求一次不改变 DOM 内容或 framebuffer 像素的 paint-only diagnostic repaint，使静态 App 也能
采集真实 command invocation；该额外工作只属于 profiling capture，不能混入常规性能基线。

### 必填与约束

- `format`、`type`、`frame`、`totalUs`、`stagesUs` 必须存在；所有时间为非负整数微秒；
- `frame` 在同一 session 严格递增；重复、回退或损坏记录必须被工具报告，不能静默排序；
- `totalUs` 是 producer 测得的 frame wall time；`stagesUs` 可以存在未归因间隙，工具不得
  宣称阶段之和等于 total，除非 producer 明确给出 `timingComplete: true`；
- `stageSpans` 是可选的真实阶段 span 数组，每项包含 `name`、相对于 frame trace 起点的
  `startUs` 和 `durationUs`。它用于绘制实际时间线；缺少该字段时，工具只能展示 `stagesUs`
  的累计构成，不能推断阶段开始/结束时间；span 之间的空白必须保留为未归因间隙；
- `commandSpans` 是可选的有界命令事件数组；达到数量或行预算上限时必须输出
  `commandSpansTruncated: true`。它不能替代 `commands` 的跨帧聚合，也不能将未采集的
  composite 或 host 工作分配给某个元素。存在 `stageSpans` 时，查看器按时间重叠将每个命令
  关联到占比最大的阶段；没有重叠时显示为 `unattributed`，这不是 producer 对嵌套关系的声明。
  `rect` 是命令完成 raster 后的有界整数矩形；查看器只有在它与最终 `dirtyRects` 相交时才显示
  “脏区内的实际重绘证据”，这代表空间重叠，不代表 DOM mutation 根因；
- `commands[].owner` 只能是唯一且受限 ASCII `id:<id>`，或会话内 opaque `n<N>`，也可为
  `unattributed`；不得输出裸指针、DOM path、文本、arena 地址、文件密钥或设备物理地址；
- `commands` 是可选归因。每项带 `type`、`owner`、`us`、`pixels`、`samples`；没有可靠归因时，UI 必须显示“无法归因到元素”，不能
  把整层耗时错误归给第一个元素；
- command 聚合最多 64 项，owner 最多 64 个；出现上限或行预算截断时必须分别输出
  `commandsTruncated` / `nodesTruncated`。frame 的 `timingComplete` 保持 false，且没有可靠时钟
的样本只累计 `commandInvalidSamples`，不能用 0 us 伪装为完整或有效测量；
- trace 必须有记录数、单行字节数和 session 总大小上限；设备侧默认只保留最近窗口，
  导出到主机后再长期保存。

### Profiling 开销 A/B

使用 `tools/render_trace_profile_ab.py` 对同一 shell、App 与确定性输入执行交替的 baseline/profiled
capture；工具会拒绝复用已有输出目录，并将每轮 stdout/stderr、所有 BMP、profiled JSONL、shell SHA-256、
命令、逐帧 hash、p50/p95 写进单一归档。任何一帧 BMP 不同即失败，不输出性能结论。

```powershell
python tools\render_trace_profile_ab.py `
  --shell build\render-trace-image-build\Release\jellyframe_desktop_shell.exe `
  --app tests\fixtures\apps\ws147_touch_drag_latency `
  --output build\trace-profile-ab-20260910 `
  --frames 120 --rounds 5 --warmup 1 `
  --viewport-width 172 --viewport-height 320 `
  --frame-event 1:pointer-down:20:20 `
  --frame-event 2:pointer-move:130:20 `
  --frame-event 3:pointer-up:130:20
```

`summary.json` 的 `captureProcessUs` 包含 shell 启动、确定性 capture、BMP 写盘，以及 profiled
侧的 trace 采集/序列化；它适合判断“开启这一诊断是否有可解释额外成本”，不适合声明 Render Core
单帧、实机 FPS、DMA/panel 或命令总耗时。只允许与相同二进制、输入、机器和电源状态下的归档作比较。

## 4. VS Code 逐帧工具目标

第一阶段界面应提供：

1. 按 frame 的列表，显示 total、FPS 等效值、action/reason、dirty rect 数与面积；
2. 阶段构成条和实际 span 时间线，点击阶段显示其绝对时间、占比、开始偏移和 producer runtime 来源；
   没有 `stageSpans` 的旧 trace 回退到累计耗时构成；
3. 画布 overlay：dirty rect、paint bounds、clip bounds，切换前后帧；
4. command/owner 排名，显示类型、受限 owner、候选像素、调用次数和耗时；有真实 span 时额外显示
   `type + owner + stage` 的跨帧聚合；
5. 跨帧聚合，按 command、stage 和 owner 汇总调用数、累计耗时与 p95，并显示截断/缺失归因；同时展示
   dirty rect 空间重叠证据聚合；
6. frame scrubber，逐帧查看“重建了什么、复用了什么、哪些区域被清除/重绘”，并对比相邻帧的耗时变化；
7. p50/p95 与最慢帧固定显示，并允许导出原始 JSONL/HTML；
8. 异常帧筛选和历史趋势，点击趋势条可跳转到对应 frame；
9. 缺失 trace、设备只提供 aggregate 或 command 未归因时显示来源和限制。

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

仓库提供 `tools/benchmark_compare.py` 作为统一比较入口。Cairo、SDL、LVGL 或其他适配器只负责输出
`jellyframe.benchmark.run.v0` JSON；工具会检查 workload、viewport、pixel format、抗锯齿、
full/dirty mode、运行环境、warm-up，以及输出验证方法/基准/容差。任一条件不一致或任一侧视觉验证
未通过时，整组结果标记为 `not-comparable`。只有成对的 pixels 与正耗时样本才派生 MPix/s，
不会从平均像素数猜测吞吐率。

当前基于代码和已有 telemetry 可以提出、但尚不能宣称为 benchmark 事实的工作假设是：

- JellyFrame 可能在有界 CSS 子集、跨平台同一 DOM/布局语义、可裁剪 feature profile 和
  dirty/update 诊断上更容易控制；这些优势需要矩阵实验验证；
- JellyFrame 预计在 CPU 阴影、rounded coverage、大面积渐变、复杂文本 shaping 和缺少
  GPU/硬件合成的场景更慢；已有设备归因已显示 BoxShadow 是 rounded workload 的主要耗时，
  但该结论只适用于对应 fixture，不能外推全部 App；
- 若 present/DMA 占主导，继续优化 Core paint 不会改善实机帧率；若 layout/script 占主导，
  应优先减少重建或 App 更新范围，而不是改 rasterizer。

当前 Windows-only `jellyframe_cpu2d_compare` 已提供三个 CPU 2D primitive 对照。三者均在同一进程中
使用 JellyFrame 与 memory-DIB GDI、172x320 RGB surface、固定 30 次 warm-up 和相同样本数：

- `opaque-fill` 对照不透明全屏填充，要求归一化 RGB 完全一致；
- `horizontal-gradient` 与 `vertical-gradient` 分别对照不透明横向/纵向渐变和 GDI `GradientFill`，
  要求归一化 RGB RMSE 不超过 1.0。当前 fixture 的最大通道误差为 1，这是两套整数端点插值约定
  的已量化差异，不允许以该容差掩盖更大视觉误差。

在本开发机的 100 样本 A/B 中，复用首个 clipped row 后 JellyFrame 横向渐变 p95 从 437.7 us 降至
4.3 us，输出 digest 保持 `177877a38d09ae83`；同轮 GDI 约为 4.4 us p95。该数字只证明 172x320、
不透明、横向、矩形渐变 primitive 的改动有效，不能外推到完整 UI、设备 FPS、圆角/透明渐变、文本、
GPU 或其他机器。圆角、文本以及 LVGL 实机对照仍需分别建立等价 workload 和输出质量门禁。

## 7. 阶段出口

### 第一阶段：已交付

- source-aware JSON/HTML report 工具；
- 单帧 pipeline report、设备 aggregate telemetry、microbench 的明确分层；
- trace V0 的输入格式和回归测试。

### 第二阶段：部分交付

- Win32 shell 已在显式选项下产生 bounded frame JSONL；
- trace report 工具会报告重复、回退或非法 frame number，不静默排序或伪造帧；
- 每帧补齐阶段 timing、dirty/pipeline counters 和 frame update reason；
- VS Code Render Trace 面板读取 trace，支持 frame scrubber、阶段占比、dirty 覆盖率条、最多 32 个 dirty 矩形、当前帧截图、command 归因和跨帧聚合；新 trace 还显示 command span 的阶段关联和跨帧 p95，截断仍明确标注为下界；
- `.jfcapture` 回放可显式生成同目录 bounded Render Trace，并在状态视图中保留打开入口；
- 命令/节点归因的 owner-token、边界、截断和正确性门槛已在
  [专用 RFC](render_trace_command_attribution_rfc_zh.md) 冻结；opt-in Core owner token、value-only
  raster observer、Win32 有界聚合/producer 和 VS Code 命令排行均已交付，仍不对未拆分的 composite/
  transform/host callback 工作作元素耗时猜测。

### 第三阶段：设备 profile

- 已冻结 [Device Performance Profile V0](device_performance_profile_rfc_zh.md)：默认关闭，
  只输出固定窗口的阶段/percentile/counter 汇总；首个 ESP32-S3 接线必须遵守其窗口、
  时钟、隐私、开销 A/B 和失败标记契约；
- port 以该显式 profiling 配置提供阶段窗口 aggregate；逐元素、逐 command 或每帧 wire
  trace 不是第三阶段的前提，也不能由 aggregate 推断；
- 以真实 developer-image workload 复核 Core/Runtime 优化收益；
- CPU 2D 的 opaque-fill、horizontal-gradient 与 vertical-gradient GDI 基础对照已交付，但尚不足以
  代表完整库；
- 桌面 CPU 对照的 workload 边界已冻结，见
  [CPU workload 矩阵](render_performance_cpu_workload_matrix_zh.md)。当前三个矩形
  primitive 已可 comparable；圆角和文本已有 Core 内部 probe，但由于 coverage、字体
  fallback、shaping 和宿主 `TextPainter` 差异，跨库适配仍不能直接宣称 comparable。
  嵌入式 UI 对照以固定状态检查和稳定性日志为正确性门槛，不把逐像素 readback 作为
  默认硬件前置条件。
- [嵌入式 UI 对照 workload V0](render_performance_embedded_ui_workload_zh.md) 已完成 fixture
  qualification 和四 workload 首轮矩阵。首轮部分 percentile 受旧 histogram 上限影响；随后
  `local_update` 使用扩大 range 的精确 pair 完成交错复测并通过 `PASS (visual-equivalent-only)`。
  `static`、`scroll`、`full_repaint` 尚未完成同 pair 的 range retest，因此四 workload 总体性能
  结论仍保持开放。

### 第四阶段：面向 App 作者的闭环

- package report、Render Trace、设备 telemetry 和 microbench 已接入 VS Code 项目级性能会话：
  自动发现最新输入，一次多选后将来源快照、SHA-256、源码提交与 Runtime/Core 身份写入
  `.jellyframe/build/performance/<session-id>/`，并维护最近 20 次成功或失败历史。各来源仍独立
  展示，不进行桌面/设备耗时混算；`.jfcapture` 仍通过其生成的 report/trace 间接进入会话；
- 增加低开销、有界的实时 trace 环形缓冲，桌面默认可用，设备仍默认只采集 aggregate；
- 建立“输入/脚本更新 -> mutation -> invalidation -> layer/command -> dirty 区”的可靠
  关联，仅在 producer 确实提供证据时展示元素根因；当前已完成 deterministic desktop capture 的
  frame-local source -> mutation generation -> invalidation 记录，并对安全唯一 `id` 提供 owner bounds
  与 dirty rect 空间关联；脚本 mutation 已通过 opt-in Runtime observer 传播到直接被修改的安全 owner，
  查看器同时提供同 owner 的命令/dirty evidence 关联，但真正 mutation 根因判定仍需更多 producer 证据，
  空间命中不能单独证明因果关系；
- 项目级有界历史和身份记录已交付；同 workload、同来源、同 board/viewport 且 ABI 兼容时的自动
  版本回归判定已交付，身份不足或不一致时拒绝比较；设备 aggregate 的多会话趋势图与有界
  JSON/HTML 归档也已交付，严格按 case/profile/board/viewport/ABI 分组且不补齐缺失指标；
- 完成嵌入式 UI 设备矩阵，并为圆角/文本找到满足 workload 矩阵条件的适配器后，再
  冻结对外的性能比较结论。
