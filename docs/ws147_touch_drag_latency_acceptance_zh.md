# WS147 触摸拖动响应验收

> 最后更新：2026-09-08；适用版本：0.6.0-dev；适用范围：ESP32-S3 WS147 port；候选提交：`b8f8efbb`、`6f4caf5f`、`159f7ee2`

## 目的

验证 WS147 上连续触摸拖动不会因 UI 帧、渲染或 panel present 较慢而逐个重放旧坐标。
本验收覆盖端到端的触摸采样、`BoardInputQueue`、`InputController` 和实际 panel 输出。
它不替代通用触摸校准、脚本运行时、Device OS 生命周期或整体帧率验收。

候选实现的预期语义如下：

- 相邻 `PointerMove` 只保留最新坐标；移动采样是可替换状态，不是离散操作。
- `PointerDown`、`PointerUp`、wheel、focus、activate 和 text 保持原始顺序，绝不跨越合并。
- 队列满时先移除最旧的 `PointerMove`，再接收最新输入；若队列中没有可替换 move，才拒绝新事件。
- WS147 的 `PointerUp` 使用最后有效触点坐标，不使用按下起点。

## 前提和输入

1. 使用 WS147，屏幕 profile 为 `rect-172x320`，CPU 240 MHz，FreeRTOS 1 kHz tick。记录板卡、USB endpoint、固件 SHA-256、ESP-IDF 版本和完整 `sdkconfig` provenance。
2. 固件必须至少包含候选提交 `159f7ee2`；不得只刷写旧 Developer Image 后凭手感判定。
3. 使用真实可交互 App，页面至少有：
   - 一个水平 `<input type="range">` 或等效滑块，宽度不少于 120 px，最小到最大值可见或可记录；
   - 一个可纵向连续滚动的容器；
   - 一个独立按钮或可点击项。
4. 保留完整 USB/JFDP 安装记录、冷启动日志和从开始操作到结束后至少 10 秒的串口日志。测试期间不要同时打开第二个原始 serial monitor 或占用 JFDP endpoint。
5. 建议以 120 fps 或更高帧率录像同时拍到手指和屏幕；录像用于量化抬手到最终视觉状态的延迟。没有高速视频时仍可做功能验收，但报告必须标为无定量视觉时延证据。

## 构建和刷写

从 `ports/esp32s3-idf/` 使用隔离 build 目录。可按实际 Developer Image 的 defaults 叠加；不要把 Timer 或 benchmark defaults 意外带入产品 App 验收。

```powershell
idf.py -B build-ws147-touch-drag `
  -D "SDKCONFIG=build-ws147-touch-drag/sdkconfig" `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_bringup.defaults" build
idf.py -B build-ws147-touch-drag -p COMx flash monitor
```

若验收的是已安装 Developer Image App，刷写后通过已配置的 provider/VS Code 或 CLI 安装同一份已检查的 `.jfapp`，并在报告中保存 bundle SHA-256、manifest identity 和 registry generation。若本次 port 使用不同的发布 defaults，命令可调整，但必须归档最终 sdkconfig，不能只写口头配置。

## 测试步骤

每个 case 至少完成 30 次；快速往返拖动至少完成 10 个连续周期。每次开始前等待界面稳定，避免把启动期首帧计入时延。

| Case | 操作 | 期望行为 |
| --- | --- | --- |
| T1 点击定位 | 在滑轨左、中、右各点击 10 次 | 滑块立即跳到点击位置；值、拇指和页面状态一致。 |
| T2 慢速连续拖动 | 从左到右约 1 秒，再从右到左约 1 秒 | 拇指连续跟随，不出现阶梯式回放旧轨迹。 |
| T3 快速往返 | 以正常手指可达速度左右往返 10 次 | 拇指保持接近当前手指位置，不在手指已反向后继续向旧方向追赶。 |
| T4 末端抬手 | 分别在 25%、50%、75% 位置快速拖动后立即抬手 | 最终值对应最后采样位置；抬手后至多允许一次正在进行的屏幕刷新，不得继续多帧移动。 |
| T5 滚动隔离 | 在滚动容器中快速上下拖动并立即松开 | 内容停在最终拖动位置附近，无拖动结束后持续惯性回放；滑块状态不被污染。 |
| T6 离散输入隔离 | T2/T3 后立即点击独立按钮 | 点击只触发一次，且没有遗留 drag/move 使按钮误触或失去响应。 |
| T7 压力恢复 | 连续执行 T3、T5 各 30 秒，再重复 T1/T4 | 无卡死、watchdog、reset、I2C/DMA/SPI/panel error；后续点击和拖动仍正常。 |

## 日志和量化判据

候选固件的 `ui_task_frame` 日志应包含：

```text
input=<n> queue_left=<n> moves_coalesced=<n> input_dropped=<n> present_us=<n> ok=<0|1>
```

验收报告必须统计并解释以下字段：

| 指标 | 通过条件 |
| --- | --- |
| `moves_coalesced` | 在 T2/T3 中大于 0 是正常且预期的，证明过期 move 没有逐条回放。它不是错误计数。 |
| `input_dropped` | 从冷启动到全部 case 结束始终为 0。若非 0，报告必须失败或单列为 queue-pressure failure，不得以视觉正常替代。 |
| `queue_left` | 操作停止后，应在两次 `ui_task_frame` 日志内回到 0；不得跨日志持续增长。 |
| `present_us` / `ok` | 所有与 T1-T7 重叠的 present 都应 `ok=1`。异常长 present 必须逐条保留原始日志并与录像时间线关联。 |
| 最终值 | T1、T4 的最终 slider value 与最后触点所在轨道位置一致，允许不超过一个声明 step 的量化差。 |
| 抬手视觉延迟 | 有高速视频时，T4 的"手指离开"到"拇指到达最终位置"的 p95 不超过 100 ms，最大值不超过 150 ms。若显示刷新率或录像能力不能可靠量化，应明确标为 not-measured，而非声称通过该项。 |

`moves_coalesced` 可单调累计；请记录运行开始和结束值，以及增量。`input_dropped` 同样为累计值，验收使用增量而不是不同刷机间的绝对值比较。

## 失败归因

- `moves_coalesced=0` 且拖动仍滞后：先确认实际固件是否包含候选提交、触摸任务是否产生 `PointerMove`，再检查是否运行了非 retained/script UI 路径。
- `input_dropped>0`：队列在没有可淘汰 move 时饱和。保存完整事件序列和 queue/present 日志，归为端侧输入容量或消费者调度问题。
- `queue_left` 在停止操作后不回落：UI task 或 frame loop 未及时消费输入；不要只通过增大队列容量掩盖。
- 最终值错误但 `input_dropped=0`：检查触摸坐标映射、`PointerUp` 最终坐标、range hit-test 和脚本事件处理。
- 最终值正确但视觉持续追赶：检查 dirty invalidation、render/present 时间和 panel DMA，而不是再次修改输入事件顺序。
- reset、watchdog、I2C、DMA、SPI 或 panel error：本验收失败，需单独归档 reboot 前后的完整日志。

## 归档和结论

创建 `ports/esp32s3-idf/test_artifacts/ws147-touch-drag-latency-YYYYMMDD/`，至少包含：

- `report.md`：候选 SHA、硬件/固件/SDK provenance、每个 case 的次数和 verdict；
- `summary.json`：每项次数、`movesCoalescedDelta`、`inputDroppedDelta`、最大 queue depth、present p50/p95/max、视频时延样本和 verdict；
- 原始串口日志、build/flash log、最终 sdkconfig、manifest、`.jfapp` SHA-256；
- 原始视频或其可复现的帧时间标记；
- 失败时的最小复现步骤和未裁剪日志。

仅当 T1-T7 全部通过、`inputDroppedDelta=0`、无平台错误且 T4 的最终位置正确时，才可将本 patch 标记为 WS147 input responsiveness PASS。该 PASS 只证明这个具体 port/profile 的触摸响应，不扩大为其他板卡或通用运行时结论。
