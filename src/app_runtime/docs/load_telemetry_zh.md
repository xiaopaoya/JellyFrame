# App Load Telemetry

`AppLoadTelemetry` 是给宿主调度、动态调频和低功耗策略使用的小型硬件无关摘要。它不读时钟、
不启动线程、不碰驱动，也不会改变帧循环。宿主把当前帧已经掌握的事实喂给它，再把结果映射到
自己的产品策略。

头文件：`src/app_runtime/app_load_telemetry.h`

相关预算快照头文件：`src/app_runtime/app_budget.h`

`AppBudgetSnapshot` 是配套的计数视图。它收集当前 app instance id、role/state、
service request/completion 队列、host handle、handle bytes、app font 数量、
system-event 队列深度、frame-loop callback 上限、animation 数量，以及宿主能提供时的
script timer/listener/detached-node 计数。DOM/CSS/render/layout/layer、display command、
dirty rect、framebuffer pixels、resource bytes 和 script watchdog 等静态 `HostBudgets`
上限也放在同一个 snapshot 中。

这个 snapshot 刻意保持只读、只存计数。它不遍历 DOM、不检查 framebuffer 像素，也不分配页面级
存储。桌面壳可以在每次 capture 时打印；MCU port 可以把它用于串口 diagnostics、watchdog
策略或 crash report。

## 输入

`AppLoadTelemetryInput` 复用 JellyFrame 已有契约：

- `AppFramePolicy`：当前 app 是否接收输入、泵动 timer、泵动动画、提交画面。
- `AppServiceActivityPolicy`：当前是否允许 network/audio/sensor/location 活动。
- `FrameLoopWorkPlan`：本帧有界 input/timer/rAF 工作。
- `FrameUpdatePlan`：渲染管线是 idle、repaint 还是 rebuild。
- dirty-region 摘要：可传 `DirtyRegionResult`，也可在宿主已经记录过时只传
  `dirty_region_mode` 和 `dirty_area_percent` 标量。
- service request/completion 队列深度和容量。
- active animation 数量和 `present_pending` 状态。

该 helper 不分配大 buffer，也不检查 DOM、JS、framebuffer 像素或硬件状态。

## 等级

`analyze_app_load(...)` 返回：

| 等级 | 含义 |
| --- | --- |
| `sleep-ok` | 没有可见帧工作，也没有待处理 service。若外部唤醒源允许，宿主可进入 idle 或浅睡。 |
| `low-frequency-ok` | app 仍可见或存活，但没有活动动画、积压和重建。可以接受较低 CPU 频率。 |
| `normal` | 有普通 UI 工作，但仍在预算内。 |
| `boost-needed` | full rebuild、大 dirty area、队列压力或 callback 积压提示应临时提频或尽快调度。 |
| `overloaded` | 重绘/重建较重，同时存在积压或 present 压力。宿主应考虑丢动画帧、降低动画 FPS 或推迟非关键工作。 |

`sleep_ok`、`low_frequency_ok`、`boost_recommended` 和
`drop_animation_frame_recommended` 是给小型 port 使用的便捷布尔字段，避免移植层必须 switch enum。

## 宿主建议

建议映射：

- `sleep-ok`：如果面板、音频和唤醒源策略允许，可以进入 tickless idle/浅睡，并释放显示或频率锁。
- `low-frequency-ok`：UI task 保持可运行，但使用较低频率。
- `normal`：使用产品默认交互频率。
- `boost-needed`：为当前帧或 service pump 获取短时 PM/频率锁。
- `overloaded`：优先保证 UI task 响应；先降低动画预算或推迟后台 service，再避免无界帧时间。

`present_pending` 通常只应在 host frame sink 仍持有下一帧需要复用的 buffer 时为 true。此时 UI task
应等待 flush completion，或只做不会触碰这些 buffer 的非渲染工作。

## 边界

这是建议性遥测。核心只报告负载；宿主仍负责：

- 真实 CPU 调频；
- 浅睡/深睡进入；
- PM lock；
- task 优先级；
- watchdog feed；
- worker 调度；
- panel DMA 或 flush-complete gate。

该 helper 刻意偏保守。比起隐藏 full-frame rebuild 或 service 队列积压，过早报告
`boost-needed` 更容易被产品策略修正。
