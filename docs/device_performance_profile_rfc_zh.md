# Device Performance Profile V0（提案）

> 状态：提案；最后更新：2026-09-10  
> 范围：带 console 的 port 实机阶段窗口汇总；不定义桌面 trace、逐元素计时或 JFDP wire 扩展。

## 目标

现有 ESP32-S3 UI task 已有长期累计的 `port_telemetry` 与
`port_pipeline_telemetry`：它们覆盖 frame/present、pipeline rebuild、compose、
RGB565 转换、DMA submit/wait、dirty/full、队列和 heap low-water。这足以证明某次
fixture 的总趋势，但不能可靠回答“一段固定交互中哪一阶段拖慢了 p95”。

本 profile 为该缺口定义一个**默认关闭、有界、按固定窗口汇总**的端侧数据契约。它必须：

- 只测量真实 Device OS/board 路径，不能以 Win32 trace 或 Core microbench 代替；
- 不在每帧输出日志，不输出 DOM、文字、资源路径、地址或逐元素/逐 command 数据；
- 在测量窗口结束后输出一条机器可读记录，并明确测量缺口；
- 允许将 Core/Runtime 候选与 port/panel 候选分开判断，而不将彼此时间相加；
- 在未启用 profile 时不增加默认产品路径的计时、分配或串口 I/O。

这不是帧率承诺，也不替代视觉、稳定性、温升/供电或 DMA 正确性验收。

## V0 配置与窗口

port 提供一个显式 build/profile 开关，例如
`JELLYFRAME_ESP32S3_DEVICE_PERFORMANCE_PROFILE`；默认 `n`，不得由 Developer
Image、普通 launcher 或 App 自动开启。首个 ESP32-S3 实现只支持带 console 的 retained
UI-task fixture；Developer Image 的 JFDP `Logs` 响应没有分页 cursor，不能承载一个完整窗口。
未来必须增加独立 typed JFDP profile payload 和完整性语义，才可对安装 App 导出。实现可以先采用 Kconfig；profile 名、窗口帧数
和 warm-up 帧数必须记录在输出中。

V0 固定如下边界：

| 项目 | V0 规则 |
| --- | --- |
| warm-up | 默认 30 个实际 present；不计入统计 |
| 测量窗口 | 默认 120 个实际 present；范围 30–600 |
| 样本 | 仅 `FrameUpdateAction != None` 且已进入实际 present 路径的帧；失败 present 仍计样本和错误 |
| 空闲帧 | 单独计数，不进入 latency percentile |
| 输出频率 | 每个完整窗口最多一组五条短记录；任务停止前可选择输出一个标记为 `partial=1` 的不足窗口 |
| 内存 | 固定 histogram、固定 counters；不允许为 trace 建动态容器、字符串或每帧日志 |
| 时钟 | `esp_timer_get_time()` 单调微秒；差值在采样点立即转换为非负 `uint32_t` 微秒 |

窗口只允许在启动和结束时写日志。为避开 ESP-IDF 的日志缓冲限制，一个窗口连续输出
`device_profile`、`device_profile_timing`、`device_profile_pipeline`、`device_profile_present`、
`device_profile_counters` 五条短记录；五条共同组成一个窗口，host parser 必须合并，且不得把它们视为五个样本。串口/JTAG 输出、JFDP 查询、截图与 host 文件 I/O
不得落在窗口内；若不能保证，结果必须标为 `contaminated=1`。

V0 host 工具只接受一个完整窗口：五条记录必须都有相同的非负整数 `window`，同一
record kind 不得重复。任意一条缺失、`window` 缺失或不合法、重复 record，或一个文件
内包含多个窗口时，工具必须拒绝输入，不能用其他 `port_telemetry` 行、长期累计值或相邻
窗口补齐。`partial=1` 仍表示一个结构完整但样本不足的窗口，报告必须保留该标记而不能
把它当作完整 120-frame 结论。

## 阶段语义

每个阶段都是同一 UI task 内的 wall time，不是 CPU cycle，也不是可相加的跨 task 时间。
`frame` 是从 UI loop frame start 到该轮 loop 结束；它可能含调度和未归因时间。各阶段可有
间隙或重叠，故 `timing_complete=0` 是 V0 的常态。

| 字段前缀 | 边界 | 归属 |
| --- | --- | --- |
| `input_us` | 端侧输入队列/事件分派 | Runtime/port integration |
| `script_us` | UI task 实际等待或消费 script worker 已发布 frame 的时间；未可靠接线时省略并列入 `missing` | Runtime |
| `planning_us` | dirty flags 到 `plan_frame_loop` 完成 | Runtime/Core integration |
| `pipeline_us` | render tree、layout、layer tree、input bind 的 pipeline rebuild | Render Core + Runtime integration |
| `paint_us` | `SoftwareCompositor::render_into`，含其在该 task 内的 raster 工作 | Render Core（不含 panel） |
| `present_us` | 进入 `present_frame`/packed flush 到返回的 wall time | port + panel path |
| `convert_us` | RGBA→RGB565、scratch copy 与 panel buffer byte-order conversion | port boundary |
| `dma_submit_us` | panel draw/submit 调用 | board port |
| `dma_wait_us` | 等待 panel/DMA completion | board/DMA/panel |
| `frame_us` | frame loop 的端到端 UI task wall time | 仅作用户体验趋势 |

`pipeline_us` 可以附带 `render_tree_us`、`layout_us`、`layer_tree_us`、`input_bind_us`。
这些子项只在 rebuild 帧有样本；报告不得除以全部 frame 数伪装为每帧成本。

## 机器可读记录

V0 采用有界键值日志，保持既有 `port_telemetry` 解析兼容，并增加稳定标识：

```text
device_profile format=jellyframe.device.profile.v0 case=scroll_benchmark_cumulative \
profile=ws147-perf-v0 window=1 warmup_frames=30 frames=120 present_frames=120 \
full_frames=4 dirty_frames=116 idle_frames=18 pipeline_frames=8 timing_complete=0 missing=script_us contaminated=0 partial=0
device_profile_timing window=1 frame_us_p50=28600 frame_us_p95=41700 frame_us_max=52100 \
input_us_p50=180 input_us_p95=310 planning_us_p50=180 planning_us_p95=310 \
device_profile_pipeline window=1 pipeline_us_p50=0 pipeline_us_p95=7200 \
paint_us_p50=10400 paint_us_p95=16800 present_us_p50=15100 present_us_p95=22800
device_profile_present window=1 convert_us_p50=2900 convert_us_p95=4800 \
dma_submit_us_p50=220 dma_submit_us_p95=390 dma_wait_us_p50=11300 dma_wait_us_p95=17100
device_profile_counters window=1 dirty_rects_avg_x100=135 dirty_pixels_avg=18944 \
converted_pixels=2273280 packed_bytes=4546560 present_failures=0 \
internal_free_min=80399 psram_free_min=4210688 stack_free_words=4280
```

要求：

- 所有 `_us` 为整数微秒；percentile 来自固定 bucket histogram，需输出
  `histogram_bucket_us` 与 `histogram_ceiling_us`；上界 bucket 代表“至少该值”，不可当作精确值。
- `*_p50`、`*_p95`、`*_max` 对同一阶段使用相同样本集合；`pipeline_us` 仅在
  `pipeline_frames` 所示的 rebuild 样本中记录。若该数为 0，必须省略 pipeline percentile，
  不能将非 rebuild 帧的 0 混入或冒充为快速 rebuild。
- `dirty_rects_avg_x100` 用整数保存平均值乘 100，避免浮点；`converted_pixels`、
  `packed_bytes` 用累计值。
- `present_failures` 包括 UI task 可观察到的 present/flush 失败；watchdog、panic、brownout、reset
  与不可归因的板级错误应由完整 artifact 及 boot log 报告，不能伪报为 0。
- record 需要包含 fixture、firmware/Core/Runtime identity、board、viewport、panel bus/pixel format
  和 profile/Kconfig 身份；五条记录的任一条缺失都必须在 artifact 中明确说明，不能由
  其他窗口或长期累计值补齐。

## 实现界限

ESP32-S3 的首个实现只复用已有计时点：`render_and_present()` 的 compose/present 和
`Rgb565PackedFlushMetrics`，UI loop 的 input/planning/rebuild，以及既有 heap/queue counters。
它不修改 Render Core ABI，不启用 raster command observer，不在 script worker 或中断中调用
`esp_timer_get_time()`，也不因 profile 改变 dirty、compositor、panel flush 或任务优先级策略。

优先顺序：

1. 抽取无分配的 `DeviceProfileWindow`，为已有阶段累计固定 histogram/counter；
2. 加 Kconfig 默认关闭、窗口边界和五条有界 summary；
3. 为 WS147 static、drag/scroll console fixture 接线；Developer Image 需先增加独立 typed
   JFDP profile payload。script animation 也需要单独的 worker/UI 时钟与统计边界；
4. 以同一 firmware 的 profile OFF/ON A/B 验证像素、成功/错误计数和 p95 开销；
5. 再由 provider/VS Code 读取 artifact，而不是让 IDE 高频轮询设备。

## V0 验收门槛

- profile OFF 与 ON 使用同一输入时，视觉/像素证据、present 成功数、错误和恢复行为一致；
- ON 的输出仅在窗口边界出现，单个窗口缺失、污染或计时失败必须明确标记；
- 将截取、拼接和重连后的 console log 交给 host 工具时，缺失、重复或多窗口记录必须
  被拒绝，不得生成部分或混合报告；
- 120 个 present 的 profile ON/OFF A/B 报告 p50/p95/max，并记录计时本身的开销；
  开销超过 p95 的 3% 或造成任何队列/内存/稳定性回归时，维持 `experimental` 并先优化；
- 首个实现至少在静态和连续拖动/滚动两个真实 console fixture 上通过；Developer Image 与脚本驱动
  动画作为下一项独立 profile 验收，未实现前必须标为 `not-tested`；
- 没有 device trace 时，VS Code 只能展示 aggregate window 和限制，绝不虚构 element/command attribution。

## 非目标与后续

V0 不做逐元素/command 实机 trace、per-frame JFDP stream、跨核因果追踪、功耗采样、
GPU 对比或自动性能评级。若之后需要逐元素归因，必须另行设计采样率、隐私/资源边界、
跨 task clock/ownership 与显著 profiling perturbation 的测量；不能从本窗口汇总推断。

相关资料：[Render 性能观测](render_performance_trace_zh.md)、
[端口工作指南](porting_work_guide_zh.md)、
[command owner 归因 RFC](render_trace_command_attribution_rfc_zh.md)。
