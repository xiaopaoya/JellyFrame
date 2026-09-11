# Device Performance Profile V0 实机验收要求

> 状态：移植侧执行稿；最后更新：2026-09-10；适用版本：0.6.0-dev
> 适用范围：ESP32-S3 console-backed retained UI fixture  
> 不适用：Developer Image/JFDP 导出、脚本 worker 动画、逐元素 trace、功耗验收

本文是 `Device Performance Profile V0` 的实机执行要求。它用于回答同一块板、同一
输入和同一固件路径下，开启 profile 是否改变了画面、稳定性或帧耗时。它不是 FPS
承诺，也不能用来推断某个 DOM 元素或 paint command 的耗时。

## 1. 前置条件

### 1.1 工具与版本

- ESP-IDF 5.3 或更高版本；必须记录 `idf.py --version`、IDF commit/版本和 Python 版本。
- 使用与被测固件完全相同的 JellyFrame 主线 commit、Render Core 版本/ABI、Runtime
  版本和 SDK lock。
- 使用同一块 WS147、同一 USB 线、同一供电方式和同一显示/触摸连接状态。
- 串口/JTAG 只能由一个采集进程独占。profile 测量窗口内不得并行运行 JFDP provider、
  VS Code 轮询、截图工具或另一份 serial monitor。
- 关闭会改变调度或日志量的额外 debug/profile 选项；只允许显式加入本 profile overlay。

### 1.2 固件配置

使用独立 build 目录，禁止在同一个 build 目录中交替覆盖 sdkconfig：

```powershell
# OFF：控制组
idf.py -B build-ws147-device-profile-off `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_scroll_benchmark.defaults" build

# ON：实验组
idf.py -B build-ws147-device-profile-on `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_scroll_benchmark.defaults;sdkconfig.ws147_device_performance_profile.defaults" build
```

两组必须使用相同的 target、编译器、源码、资源、优化级别、board profile、viewport、
panel bus/pixel format 和输入 fixture。ON 组必须确认：

```text
CONFIG_JELLYFRAME_ESP32S3_DEVICE_PERFORMANCE_PROFILE=y
CONFIG_JELLYFRAME_ESP32S3_DEVICE_PERFORMANCE_WARMUP_FRAMES=30
CONFIG_JELLYFRAME_ESP32S3_DEVICE_PERFORMANCE_WINDOW_FRAMES=120
```

OFF 组必须确认 profile 为 `n` 或没有该配置。不能以“日志中没有输出”代替读取
`sdkconfig`，因为任务可能未运行到窗口结束。

### 1.3 环境稳定性

开始前记录：

- 板卡序列号、固件 SHA-256、JellyFrame/Core/Runtime identity；
- 显示分辨率、panel controller、总线类型/频率、RGB565 等像素格式；
- 电源来源、电压条件、环境温度和是否连接触摸；
- 当前 flash/partition 状态、重启次数和 boot log；
- 输入脚本或人工操作的精确步骤及开始时间。

测试期间若出现 watchdog、panic、brownout、reset、DMA/SPI/panel error、触摸任务
异常退出或串口丢失，当前样本直接判为失败，不得只依据 profile 中的计数继续出结论。

## 2. 测试 workload

V0 至少执行以下两个 workload。每个 workload 都要分别执行 OFF 和 ON：

| ID | 场景 | 要求 |
| --- | --- | --- |
| `static` | 静态 retained UI | 首帧后按固定节奏触发局部 repaint，确保获得 120 个有效 active present；不得只等待空闲帧 |
| `drag-scroll` | 连续触摸拖动或滚动 | 使用固定起点、终点、步数和持续时间；覆盖 dirty rect 和 full-frame 变化 |

如果现有 fixture 无法在不改变页面的情况下产生 120 个 active present，应记录实际原因，
由工具输出 `partial=1`，该 workload 只能记为 `PARTIAL`，不能人工补写 `frames=120`。

### 2.1 每组执行顺序

1. 擦除或恢复到项目规定的相同初始状态，记录 boot log。
2. 刷入对应 OFF/ON 固件，确认刷写成功并记录固件 identity。
3. 冷启动一次，等待应用和 panel 完成初始化；初始化阶段不计入测量窗口。
4. 按固定输入脚本执行 workload。允许 30 个 active present 作为 warm-up，不纳入统计。
5. 继续执行直到出现完整的 120 个 active present profile 窗口。
6. 等待五条 `device_profile*` 记录连续输出后再停止 monitor。不要在五条记录之间断开串口。
7. 保存原始 console log、输入记录、屏幕证据和当次 `sdkconfig`。
8. 重复另一组时重新刷入对应固件，不复用上一组的 build 目录或残留日志。

profile 只允许在窗口结束时输出五条记录：

```text
device_profile
device_profile_timing
device_profile_pipeline
device_profile_present
device_profile_counters
```

五条记录必须具有相同的 `window` 非负整数。V0 host 工具只接受一个完整窗口：缺失、
重复、非法窗口号或同一文件包含多个窗口都会拒绝输入。

## 3. 证据采集

每个 workload 的 artifact 至少包含：

```text
<artifact>/
  metadata.json
  off/
    firmware.sha256
    sdkconfig
    boot.log
    console.log
    input.txt
    screen-before.jpg
    screen-after.jpg
  on/
    firmware.sha256
    sdkconfig
    boot.log
    console.log
    input.txt
    screen-before.jpg
    screen-after.jpg
  report.json
  report.html
  result.md
```

`metadata.json` 必须记录 commit、Core/Runtime/ABI、board、viewport、panel、workload、
输入脚本、测试时间和操作者。截图若不是设备 framebuffer 精确回读，必须标明为人工
视觉证据，不能声称像素级等效。

用 host 工具验证并生成报告：

```powershell
python tools\render_performance_report.py `
  --device-telemetry on\console.log `
  --output report.json `
  --html-output report.html
```

报告必须成功生成，且 HTML 中能看到 `Device aggregate telemetry`。若命令失败，先修
复采集或日志完整性，不得从日志中手工删行后重试。

## 4. 通过标准

### 4.1 Profile 记录完整性

每个 ON workload 必须同时满足：

- 五种 record kind 各出现一次，全部带相同 `window`；
- `partial=0`、`contaminated=0`；
- `frames=120` 且 `present_frames=120`；
- `present_failures=0`；
- `pipeline_frames` 只表示实际 rebuild 样本，不能把非 rebuild 帧的零值混入；
- `frame_us`、`paint_us`、`present_us`、`convert_us`、`dma_submit_us`、`dma_wait_us`
  的 percentile 使用各自正确样本集合；
- 原始 log 中没有 profile 窗口内部的额外采集 I/O 或并行 monitor 污染。

### 4.2 OFF/ON 行为一致性

在相同输入下，OFF 与 ON 必须满足：

- 首帧和最终状态视觉一致；
- 可进行设备 framebuffer 回读时，关键帧像素 hash 完全一致；
- 无回读能力时，至少提供固定角度、固定亮度和固定时间点的屏幕照片，并将结论标为
  `visual-equivalent-only`，不能标为像素等效；
- present 成功数、输入响应、dirty/full 行为和 teardown 无新增错误；
- 无 panic、watchdog、brownout、reset、DMA/SPI/panel 或触摸任务错误；
- `internal_free_min`、`psram_free_min` 和 stack low-water 不得越过既有 port 安全下限，
  且不能出现由 profile 引起的可解释内存回归。

### 4.3 性能开销

使用同一 workload 的 OFF 基线与 ON 窗口计算：

```text
overhead_percent = (on_frame_p95_us - off_frame_p95_us) / off_frame_p95_us * 100
```

通过要求：

- `overhead_percent <= 3%`；
- ON 的 p50、p95、max 不出现无法解释的明显回归；
- profile 输出发生在窗口边界，不把 console 输出、host 解析或截图时间计入 `frame_us`；
- 若超过 3%，或任何队列、内存、稳定性指标回归，该 profile 维持 `experimental`，
  当前 workload 判为失败并附上 raw evidence。

OFF 只有长期 `port_telemetry` 而没有同等 120-present 的 frame p95 时，不能计算严格
的 A/B overhead，结果最多为 `PARTIAL`，不得填写估算百分比。

## 5. 结果分类

| 结果 | 条件 |
| --- | --- |
| `PASS` | 记录完整、两组行为一致、无硬件/稳定性错误、性能开销不超过 3% |
| `PARTIAL` | 仅有人工视觉证据、窗口不足、缺少等价 OFF 基线，或某项指标无法严格比较 |
| `INVALID` | 五条记录缺失/重复/混窗、日志被截断、输入不一致、版本或配置不一致 |
| `FAIL` | 证据完整但像素/行为不一致、出现错误，或 profile 开销超过 3% |

`INVALID` 必须重新采集；不能通过删除异常行、拼接不同批次日志或手工改写 summary
变成 `PARTIAL` 或 `PASS`。

## 6. 当前明确不验收的范围

以下项目不属于 V0 通过条件，报告中必须写 `not-tested` 或单独列为后续任务：

- Developer Image 通过 JFDP `Logs` 返回完整 profile；当前 Logs 没有分页/cursor，不能承载五段窗口。
- 安装 App 的脚本 worker、JerryScript/QuickJS 路径和脚本驱动动画的跨任务归因。
- VS Code 逐元素或逐 command 实时 trace；V0 只提供 aggregate window。
- 温升、功耗、无线传输、长时间老化和 panel 物理可靠性。
- 与 LVGL、Cairo、Skia 等图形库的性能排名。

## 7. 验收结论模板

```text
Result: PASS | PARTIAL | INVALID | FAIL
Board / serial:
JellyFrame commit:
Render Core / ABI:
Runtime:
ESP-IDF:
Workload:
OFF firmware SHA-256:
ON firmware SHA-256:
Viewport / panel / pixel format:
Window: 1
Warmup / measured presents: 30 / 120
OFF frame p95 (us):
ON frame p95 (us):
Calculated overhead (%):
Present failures:
Watchdog/panic/reset/brownout/DMA/SPI/panel errors:
Pixel evidence: exact-readback | visual-equivalent-only | missing
Artifact:
Limitations / follow-up:
```

相关契约：[Device Performance Profile V0 RFC](device_performance_profile_rfc_zh.md)。
