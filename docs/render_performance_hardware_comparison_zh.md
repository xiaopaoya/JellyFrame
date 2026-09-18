# Render Core 性能硬件对照测试要求

> 最后更新：2026-09-18；适用版本：0.6.0-dev
> 状态：主线 3 硬件执行稿；`local_update` range retest 已 PASS；四 workload 总体验收仍按矩阵状态管理

本文用于比较 Render Core 或 Runtime 优化前后的真实设备表现。它补充
[Device Performance Profile V0 实机验收要求](device_performance_profile_hardware_acceptance_zh.md)，不替代该文档的窗口完整性和 profile 开销验收。

本测试可以回答：候选固件是否在同一块设备、同一输入和同一显示路径上减少了
frame、paint、present、转换或 DMA 等阶段耗时，以及是否引入视觉、稳定性或内存回归。
本测试不能仅凭串口数据回答某个 DOM 元素的耗时，也不能把桌面 microbench、设备
`frame_us`、`present_us` 和 DMA 时间相加后当成统一的 Core 耗时。

设备没有可靠 framebuffer readback 时，不要求逐像素比较，也不因为缺少像素 hash
自动阻止性能验收。必须使用固定状态检查记录确认首帧、关键中间状态和最终状态没有
可见布局、裁剪、残留或错行回归；readback/hash 只作为可选增强证据。

## 当前正式 pair

主线当前指定以下 pair；四 workload 首轮矩阵已完成，`local_update` 在扩大 histogram ceiling 后已完成交错顺序复测：

| 角色 | commit | 内容边界 |
| --- | --- | --- |
| `baseline` | `f8399989` | `35fb0cd8` 基线加同样的 512000 us profile histogram range 修复 |
| `candidate` | `c7bd65f2` | `48d3866c` 的 ASCII 码点测量缓存优化，加同样的 profile histogram range 修复 |

两侧都包含相同的 `embedded_ui_workload` fixture、字体、profile 协议和
512000 us histogram range 修复；pair 的有效差异仅为 candidate 的
`src/render_core/text_backend.cpp` ASCII 码点测量缓存优化。桌面 microbench
已观察到长文本 anywhere-wrap 的局部改善，但这不是设备端结论。

正式矩阵首轮已使用 ESP-IDF 5.3.1 完成四个 workload、两侧各三次有效窗口，但其中多个
percentile 受旧 histogram 上限影响，不能作为最终性能结论。随后 `local_update` 使用
`B1, C1, C2, B2, B3, C3` 顺序完成 range retest，六个窗口均完整，定量非回归与固定状态
检查均通过，结果为 `PASS (visual-equivalent-only)`。其余三个 workload 尚未用该精确 pair
完成同等 range retest，因此不能把 `local_update` 的 PASS 扩展为四 workload 总体 PASS。

## 1. 测试对象

每次对照包含两个固件组：

| 组 | 含义 | 要求 |
| --- | --- | --- |
| `baseline` | 优化前或对照 commit | 与 candidate 使用相同 SDK、Core ABI、编译器和资源 |
| `candidate` | 优化后 commit | 只允许包含本次要判断的代码变化和必要构建变化 |

两组都必须打开同一个 Device Performance Profile 配置，以保证阶段数据可比。profile
自身的开销应先按 V0 文档完成一次 OFF/ON 验收；不能用 profile ON 的 candidate 与
profile OFF 的 baseline 宣称优化收益。

## 2. 固定条件

每组都必须记录以下字段，任何字段不同都应将结果标为 `INVALID`：

- 板卡序列号、board、panel controller、viewport、像素格式、总线和频率；
- JellyFrame commit、Render Core 版本/ABI、Runtime 版本、SDK lock、ESP-IDF 和编译器；
- 完整 `sdkconfig`、固件 SHA-256、编译时间和优化级别；
- 电源方式、电压、环境温度、触摸连接状态和串口端口；
- App 包、资源、字体、初始存储状态和输入脚本；
- 测试工具版本、操作者、开始时间和每轮的冷启动方式。

固定条件期间不得：

- 在窗口中运行 VS Code 设备轮询、JFDP provider、截图程序或第二个串口 monitor；
- 更改 CPU/SPI 频率、任务优先级、DMA 配置、日志级别或 panel 初始化参数；
- 使用不同的 App 资源、不同的字体 fallback 或不同的初始滚动位置；
- 从同一个 build 目录交替覆盖两组 `sdkconfig`。

## 3. Workload 矩阵

最小矩阵如下。每个 workload 对两组固件各执行 3 个有效重复，建议执行 5 个重复。

| ID | 内容 | 重点阶段 |
| --- | --- | --- |
| `static-local-repaint` | 静态页面按固定节奏触发局部 repaint | baseline frame、paint、present 固定成本 |
| `text-layout-update` | 固定文本内容做等长度替换或状态更新 | style、layout、text paint、dirty |
| `drag-scroll` | 固定起点、终点、步数和持续时间的连续触摸滚动 | input、frame、paint、present、DMA |
| `full-repaint` | 明确触发全屏重绘的固定操作 | full-frame 转换、present 和内存 |

如果某个 workload 在设备上无法产生完整的 120 个 active present，应保留实际日志并
标记 `PARTIAL`，不得复制或手工补齐窗口。候选优化只在相同 workload、相同有效样本
数量下比较。

### 3.1 每次重复

1. 恢复相同初始状态并冷启动，保存 boot log。
2. 等待初始化和首个稳定画面完成，不把启动过程纳入测量窗口。
3. 执行固定输入脚本，按 V0 规则完成 30 个 warm-up present。
4. 采集连续 120 个 active present，等待五条 `device_profile*` 记录全部写出。
5. 保存原始串口日志、输入记录、屏幕证据和本次 firmware hash。
6. 结束 monitor 后再切换到另一组固件，不能在窗口中刷写或重连。

## 4. 必须采集的证据

建议目录结构：

```text
<comparison>/
  metadata.json
  baseline/
    firmware.sha256
    sdkconfig
    boot.log
    console.log
    input.txt
    visual-check.md
    result.json
  candidate/
    firmware.sha256
    sdkconfig
    boot.log
    console.log
    input.txt
    visual-check.md
    result.json
  comparison.json
  comparison.html
  result.md
```

每个重复应在 `baseline/` 或 `candidate/` 下使用独立子目录保存。`console.log` 必须
保留原始内容，不得删除异常行、拼接不同窗口或改变日志顺序。使用 host 工具生成单组
报告：

```powershell
python tools\render_performance_report.py `
  --device-telemetry candidate\console.log `
  --output candidate\result.json `
  --html-output candidate\result.html
```

对已经生成的重复 profile 报告，使用设备专用比较器汇总两侧结果。两侧 manifest 的
`conditions` 必须完全相同；`identity` 和报告路径可以不同。`reports` 按重复顺序列出，
路径相对于各自 manifest。比较器对每轮报告中的 `frameP95Us` 等窗口聚合值计算中位数、
最小值和最大值，不把这些值伪装成逐帧样本。

```powershell
python tools\device_performance_manifest.py `
  --root D:\JellyFramePerf\comparison-...\matrix `
  --workload drag-scroll `
  --side baseline `
  --output D:\JellyFramePerf\comparison-...\matrix\manifests\baseline.json `
  --conditions-json D:\JellyFramePerf\comparison-...\matrix\conditions.json `
  --commit <baseline-commit> `
  --visual-status visual-equivalent-only `
  --visual-record visual-check.md `
  --stability-status pass

python tools\device_performance_compare.py `
  --baseline baseline\manifest.json `
  --candidate candidate\manifest.json `
  --output comparison.json `
  --html-output comparison.html
```

manifest 最小形状如下。`exact-readback` 是可选的增强证据；没有可靠回读时，使用
`visual-equivalent-only`，并在 `visualEvidence.method` 和 `visual-check.md` 中记录固定
状态检查。只要性能、稳定性、输入和行为检查完整，`visual-equivalent-only` 同样可以得到
`PASS`；只有缺失或失败的视觉/行为检查才会降级或失败：

```json
{
  "format": "jellyframe.device.performance.run.v0",
  "workload": "drag-scroll",
  "identity": {"commit": "candidate", "firmwareSha256": "..."},
  "conditions": {
    "board": "Waveshare ESP32-S3-Touch-LCD-1.47",
    "viewport": "172x320",
    "pixelFormat": "RGB565",
    "warmupFrames": 30,
    "measuredFrames": 120
  },
  "visualEvidence": {
    "status": "visual-equivalent-only",
    "method": "operator-checklist",
    "record": "visual-check.md"
  },
  "stability": {"status": "pass"},
  "acceptance": {"mode": "target-improvement", "targetMetric": "frameP95Us"},
  "reports": ["repeat-01/result.json", "repeat-02/result.json", "repeat-03/result.json"]
}
```

非目标 workload（例如本轮只用于观察端到端滚动是否回归的 `drag-scroll`）可将
`acceptance.mode` 设为 `non-regression`。此时目标指标允许保持不变，但仍受 frame p95
和内存门槛约束；不得用该模式掩盖目标优化本身未达标的结果。

`metadata.json` 和 `comparison.json` 必须同时保留机器可读的原始路径、固件 hash、
profile window、有效 frames、各阶段 p50/p95/max、错误计数和内存 low-water。

`visual-check.md` 可以使用以下最小模板。它是操作者对固定检查点的记录，不要求照片、
显示回读或逐像素工具；如果某一项无法观察，必须写明原因并将结果降级为 `PARTIAL`：

```text
Visual check: PASS | FAIL | PARTIAL
Observer / time:
Baseline firmware / candidate firmware:
Input script and reset state: identical | not-identical

Checkpoint 1 - initial state:
  layout / alignment:
  clipping / rounded corners:
  residual pixels / wrong-line artifacts:
Checkpoint 2 - active input or scroll state:
  control follows input:
  dirty area updates without stale content:
  text wrapping / bottom edge:
Checkpoint 3 - final state:
  expected content and position:
  teardown / return state:
Notes:
Conclusion: visually-equivalent-only | not-comparable
```

## 5. 统计方法

对每个 workload 和每个阶段分别计算 3 或 5 次重复的结果，不把不同 workload 混合：

```text
delta_percent = (candidate_p95 - baseline_p95) / baseline_p95 * 100
```

优先使用每次重复的窗口 p95，再对重复结果报告中位数和范围。若 baseline p95 为 0，
该项不计算百分比，显示 `not-comparable`。同时报告 p50、p95、max，避免平均值掩盖
拖动中的长尾卡顿。

比较器必须保留 `histogram_bucket_us` 和 `histogram_ceiling_us`。任何 percentile
达到 `histogram_ceiling_us - histogram_bucket_us` 时，表示落入开放上限桶，只能解释为
“至少该值”；该指标必须标为 `histogram-saturated`，不得计算变化百分比或据此宣称通过。
两侧或各重复的 histogram 配置不一致时，固定条件无效，比较结果为 `INVALID`。

性能结果必须区分：

- `frame_us`：UI task 端到端趋势；
- `planning_us`、`pipeline_us`、`paint_us`：设备 task 内对应阶段；
- `present_us`、`convert_us`、`dma_submit_us`、`dma_wait_us`：设备输出路径；
- `packed_bytes`、`dirty_pixels_avg`、`present_failures`：流量和正确性辅助指标。

不能用 `frame_us = paint_us + present_us` 重新构造不存在的计时关系。阶段可能有间隙
或重叠，缺失阶段必须保留为缺失。

## 6. 通过标准

### 6.1 正确性和稳定性

每个 workload 必须满足：

- baseline 与 candidate 的输入脚本、有效窗口数和 warm-up 规则一致；
- 首帧、关键中间状态和最终状态通过固定检查点；至少记录布局、裁剪、残留、错行、
  触摸响应和最终状态是否符合预期；
- 有 framebuffer readback 时，可以附加固定关键帧 hash，但不得把它作为本测试的必要条件；
- 无法 readback 时，将 `visualEvidence.status` 设为 `visual-equivalent-only`，并保留
  `visual-check.md` 或等价的结构化检查记录；照片可以附加，但不是必需证据；
- 两组都无 panic、watchdog、brownout、reset、DMA、SPI、panel、present 或 touch task 错误；
- `present_failures=0`，不得出现新增队列丢弃或恢复路径；
- `internal_free_min`、`psram_free_min`、stack low-water 不低于既有 port 安全下限，且
  candidate 不出现无法解释的内存回归。

任一项不满足，性能数字只能作为诊断数据，不能判定优化通过。

### 6.2 性能门槛

默认门槛如下，项目若设置了更严格的 port 门槛，以更严格者为准：

- 目标阶段 p95 相对 baseline 改善至少 5%，并且 frame p95 不回归超过 3%；或
- 若目标是结构性优化而收益小于 5%，则至少证明 frame/paint/present p95 不回归超过 3%，
  并提供 microbench 或 trace 解释；
- `drag-scroll` 的 frame p95、present p95 和 max 均不得出现无法解释的长尾回归；
- candidate 的内存 low-water 不得比 baseline 下降超过 5%，除非该变化已被明确解释并
  经过 port 负责人接受；
- 若改善只出现在 `paint_us`，但 `present_us` 或 `dma_wait_us` 占主导，应明确写成
  “Core 阶段改善，端到端体验未证明改善”。

“改善”必须同时有稳定性和固定状态检查记录。单次运行、平均值下降、串口无错误但
没有行为检查记录，都不足以放行；逐像素 readback 不是必要条件。

## 7. 结果分类

| 结果 | 条件 |
| --- | --- |
| `PASS` | 固定条件完整，行为检查/稳定性通过，目标指标达到门槛，且无其他阶段回归；`visual-equivalent-only` 可以通过 |
| `PARTIAL` | 硬件数据有效但视觉检查缺失、重复次数不足或某项指标无法严格比较 |
| `INVALID` | 版本、输入、配置、窗口、日志或样本不一致；必须重测 |
| `FAIL` | 行为检查失败、出现视觉/稳定性回归、目标性能回归或内存门槛失败 |

## 8. 与桌面 microbench 的对应关系

桌面 microbench 只用于定位趋势和判断 Core 算法是否值得继续优化。硬件报告应保留
独立的 `microbenchReference` 区域，写明：

- 使用的构建 profile 和 Render Core 版本；
- 对应 probe 名称、平均或 p50/p95、迭代次数；
- 是否与 candidate 使用同一源码；
- 该 probe 与硬件阶段的可能对应关系及其限制。

以下结论禁止写入报告：

- “桌面 probe 快 20%，所以 ESP32-S3 frame 快 20%”；
- “paint p95 加 DMA wait p95 等于用户可见延迟”；
- “没有 command trace，所以最慢元素一定是某个 shadow/文本节点”；
- “串口没有错误，所以画面没有底部错行、撕裂或残留”。

## 9. 结果模板

```text
Result: PASS | PARTIAL | INVALID | FAIL
Workload:
Board / serial:
Viewport / panel / pixel format:
Baseline commit / firmware SHA-256:
Candidate commit / firmware SHA-256:
Render Core / ABI / Runtime / SDK:
ESP-IDF / compiler:
Repeats / warmup / measured presents:
Baseline frame p95 (us):
Candidate frame p95 (us):
Frame delta (%):
Baseline paint/present/DMA p95 (us):
Candidate paint/present/DMA p95 (us):
Visual evidence: exact-readback | visual-equivalent-only | missing
Visual check record: visual-check.md
Memory low-water delta:
Errors / resets / recovery:
Microbench reference:
Artifact:
Limitations / follow-up:
```

相关契约：[Device Performance Profile V0 RFC](device_performance_profile_rfc_zh.md)；
执行基础：[Device Performance Profile V0 实机验收要求](device_performance_profile_hardware_acceptance_zh.md)；
桌面观测工具：[Render Core 性能观测与对比方案](render_performance_trace_zh.md)。
