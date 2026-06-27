# 运行循环与增量更新契约


本文定义 JellyFrame 主线核心建议的宿主运行循环。它只描述硬件无关契约：输入、timer、dirty flags、重建、重绘和提交如何排序。真实线程、ISR、屏幕驱动、电源管理和 RTOS 调度由宿主负责。

## 推荐顺序

首帧：

1. 加载本地资源。
2. 解析 HTML/CSS。
3. 可选：绑定并执行 classic scripts。
4. 构建 render tree、layout tree、layer tree。
5. 渲染完整 framebuffer。
6. 通过 `HostFrameSink` 或 `embedded_framebuffer` 提交 full dirty rect。
7. 等待或确认 present 已完成，确保下一帧可以安全复用 frame buffer。
8. 清理 DOM dirty flags。

循环帧：

1. 如果上一帧 display present 仍在进行，且它仍持有 framebuffer/target buffer，宿主只应 sleep
   或处理不会触碰这些 buffer 的非渲染工作。
2. 从宿主队列取有限个输入事件。
3. 通过 `InputController` 派发 pointer/wheel/key/text/focus 操作。
4. 从宿主异步队列取有限个 completion events，例如资源、图片 decode、音频状态、网络响应或安装结果。
   completion 应尽量标记最小受影响 DOM owner，例如图片 decode 完成时标记使用该资源的
   `<img>` 节点；只有无法定位 owner 时才使用根节点 paint dirty 兜底。
5. 如果启用 JerryScript，泵动有限个 timer callback。
6. 如果有活动动画，泵动有限个 animation frame callback。
7. 读取根节点 `subtree_dirty_flags(document)`。
8. 填充 `FramePipelineCacheState`，调用 `make_frame_update_state(...)`，再调用
   `plan_frame_update(...)` 决定更新路径。
9. 按计划复用现有 layout/layer，或重建管线。
10. 新 layout 已知后，用解析出的内容高度调用 `plan_frame_repaint(...)`，确认现有
   framebuffer 尺寸是否仍匹配。
11. 用 `compute_dirty_rects(...)` 生成 dirty rectangles，或保守全帧。
12. 调用 `SoftwareCompositor::render_into(...)` 或完整 `render(...)`。
13. 通过 `HostFrameSink` 提交 dirty rectangles。
14. 如果 panel driver 使用异步 DMA，标记 present in flight，直到 flush-done 事件到达。
15. 清理 DOM dirty flags。

宿主可以把这些步骤放进一个 UI task、桌面消息循环或测试壳中，但不应在 ISR 或屏幕 flush callback 内执行脚本、layout 或渲染。
异步 worker 也不应直接执行这些步骤；它只能投递 completion event。

`HostFrameSink::present` 是帧生命周期边界。它成功返回意味着宿主已经完成屏幕 flush、已经把像素复制到
driver-owned 内存，或已经让 UI loop 在复用同一 framebuffer/target buffer 前等待 flush-done。JellyFrame
不应在 panel DMA 仍读取会被下一帧覆盖的内存时开始下一轮 render。

## `plan_frame_loop`

头文件：`src/render_core/frame_loop.h`

`plan_frame_loop_work(...)` 是一个很薄的宿主 UI task 辅助函数。它不拥有输入队列、timer 队列或
animation 队列。宿主通过 `FrameLoopPendingWork` 告诉核心当前有多少待处理输入事件、到期 timer
callback 和 pending animation frame callback，然后得到一个有界的 `FrameLoopWorkPlan`：

- `input_events_to_dispatch`：本帧最多取多少个宿主输入事件。
- `timer_callbacks_to_pump`：本帧最多泵动多少个脚本 timer callback。
- `animation_callbacks_to_pump`：本帧最多泵动多少个 animation frame callback。
- `has_more_input_events` / `has_more_timer_callbacks` /
  `has_more_animation_callbacks`：是否还有积压，宿主可据此继续调度下一帧或保持 UI task 唤醒。
- `needs_animation_frame`：是否应按宿主 animation cadence 请求下一帧。

`FrameLoopOptions` 保存每帧上限。上限为 0 是合法的，表示宿主刻意暂停该类工作，例如息屏低功耗节流。
这个 helper 不会丢弃工作，只告诉宿主本帧应消费多少。宿主可以用
`frame_loop_options_from_budgets(...)` 从 `HostBudgets` 派生这些上限。
App runtime 还提供 `AppFramePolicy`，可把 foreground/suspended、screen-on 和 low-power 状态转换为
这些预算：低功耗可保留输入/timer 但关闭动画，息屏或 suspended 会把前台输入、timer、rAF 和 present
停掉，并在恢复可见时建议首帧 repaint。
`app_runtime/app_load_telemetry.h` 还提供建议性的负载分类器，供宿主做 DVFS 或睡眠决策。宿主把当前
`FrameLoopWorkPlan`、`FrameUpdatePlan`、dirty-region 摘要、service 队列深度和 frame policy
传入后，会得到 `sleep-ok`、`low-frequency-ok`、`normal`、`boost-needed` 或 `overloaded`。
该 helper 只报告负载；真实 CPU 调频、PM lock、tickless idle、watchdog feed 和 worker 调度仍归宿主策略。
Win32 验证壳通过 `--animation-fps`、`--animation-callbacks` 和对应 frame-script 命令暴露动画预算，
因此可以在不修改 app 源码的情况下验收低功耗行为。

Animation caps 与 timer caps 分开，因此带动效的页面不能饿死输入、网络 completion 或普通 timer。
无动画页面会报告 0 个 pending animation callback，也不会请求 animation frame。

`plan_frame_loop(...)` 会把上述有界工作计划和 `plan_frame_update(...)` 合成一次调用，适合希望统一规划的宿主。
它仍然不派发输入、不泵动 timer/animation callback、不修改 DOM、不重建 layout，也不提交像素。

## 异步 Completion Events

宿主可以在 UI task 外执行慢任务，但完成结果必须通过有界队列回到帧循环。典型事件包括：

- package/resource load 完成或失败；
- image decode 完成并返回 surface handle；
- audio playback 状态变化，例如 started、ended、error；
- network fetch 完成并返回 bounded byte buffer；
- app bundle 安装/删除/升级结果。

处理规则：

- 每帧最多消费 `HostAsyncCapabilities::max_completion_events_per_frame` 个事件。
- completion event 只携带小型句柄、状态和错误码；大响应体/像素/音频缓冲由宿主资源层持有。
- 如果事件会影响 DOM 或 JS，必须在 UI task 内派发 DOM event、resolve promise-like 回调或标记 dirty。
- 事件过多时，宿主应继续调度下一帧，而不是在当前帧无限循环。
- app 切换、页面销毁或系统休眠时，宿主应取消或隔离属于旧 document 的 job。

这个约束是网络请求、媒体解码和安装式第三方 app 能安全共存的基础。它让 JellyFrame 保持单 UI owner，
同时允许 ESP32-S3/RTOS port 使用额外 task 做真正的 I/O。

## `plan_frame_update`

头文件：`src/render_core/frame_update.h`

`plan_frame_update` 不拥有 DOM，也不执行布局。它只根据当前缓存状态和 dirty flags 给出更新策略。

宿主通常应通过 `FramePipelineCacheState` 和 `make_frame_update_state(...)`
构造输入。这样 render/layout/layer/framebuffer 所有权仍留在宿主侧，但桌面和嵌入式集成共享同一种
cache snapshot 形状，减少各壳层手写状态转换的出错机会。

输入：

- `dirty_flags`：根节点聚合 dirty flags。
- `has_render_tree` / `has_layout_tree` / `has_layer_tree`：是否已有可复用管线缓存。
- `has_framebuffer`、`framebuffer_width`、`framebuffer_height`：是否已有尺寸匹配的 framebuffer。
- `viewport`：当前可视区域。
- `content_height`：当前或预估内容高度；目标 framebuffer 高度至少为 viewport 高度。

输出：

- `FrameUpdateAction::None`：缓存完整且没有 dirty，无需工作。
- `FrameUpdateAction::RepaintExisting`：只需复用现有 render/layout，重建 layer tree 并重绘当前 layout dirty rect。
- `FrameUpdateAction::RebuildPipeline`：需要重建 render/layout/layer。
- `FrameUpdateReason`：稳定诊断名称，用来说明 planner 为什么选择 idle、repaint 或 rebuild，
  包括首帧、paint-only dirty、tree dirty、缺少缓存和 framebuffer 尺寸不匹配。
- `FrameDirtyRectMode::CurrentLayout`：dirty rect 可从当前 layout 计算，适合 paint-only 变化。
- `FrameDirtyRectMode::PreviousAndCurrentLayout`：重建后比较旧/新 layout，适合文本、样式或布局变化的增量重绘。
- `FrameDirtyRectMode::FullFrame`：缺少缓存、尺寸不匹配或 viewport 无效时保守整帧。

`plan_frame_repaint(...)` 是重建 layout 后使用的第二阶段检查。它只在现有
framebuffer 尺寸仍匹配解析后的目标高度时保留 `PreviousAndCurrentLayout` 或
`CurrentLayout`。如果文本、样式或布局变化导致内容变高或变矮，宿主必须 resize 或重建
framebuffer，并执行 full frame repaint。

`FrameUpdateStatistics` 记录 planner 决策和 dirty flag 输入。`FrameRepaintStatistics`
单独记录最终 repaint 结果：多少帧真正使用 dirty rectangles，多少帧回退 full-frame repaint，
以及两侧各自的稳定 `FrameUpdateReason` 直方图。Win32 frame-script capture 会同时输出这两行，
方便开发者区分“页面确实需要 layout 工作”和“最终 framebuffer repaint 退回整帧”。

## Dirty Flags 语义

- `DomDirtyPaint`：控件值、选择状态、焦点类视觉变化等。若 framebuffer 和 layout cache 可用，可走 `RepaintExisting`。
- 不含 `DomDirtyTree` 的 `DomDirtyText` / `DomDirtyLayout` / `DomDirtyStyle` /
  `DomDirtyAttributes`：需要重建 render/layout/layer，但在 framebuffer 尺寸不变时可以尝试
  `PreviousAndCurrentLayout` dirty rect。
- `DomDirtyTree`：结构变化。Planner 直接使用 `FullFrame`，且不保留上一棵 layout tree，
  因为当前 dirty-region 逻辑会保守退回整 viewport/content rect。未来 retained-subtree
  工作可以继续细化。

同值 `textContent`、未变化 attribute 等不应制造 dirty flags。对已经只有一个 text child 的元素设置
`textContent` 时，应原地更新该 text node 并避免 `DomDirtyTree`；替换混合子树或多 child 内容仍然是结构变化。

## Dirty Region 诊断

头文件：`src/render_core/dirty_region.h`

`compute_dirty_rects(...)` 仍保留为只需要矩形的宿主使用的兼容 API。
`compute_dirty_region(...)` 返回同样的 rectangles，并额外给出：

- `DirtyRegionMode::Clean`：无需重绘。
- `DirtyRegionMode::DirtyRects`：已生成有界局部矩形。
- `DirtyRegionMode::FullFrame`：核心选择保守 full-frame fallback。

`DirtyRegionFallbackReason` 会说明为什么退回 full-frame：viewport 无效、缺少 previous/current
layout、结构性 tree dirty、找不到 dirty node bounds，或局部 rect 被裁剪后全部为空。这个接口是给宿主和测试观察边界用的诊断契约；
它不表示 retained subtree reuse 已经完整实现。

嵌入式运行循环应优先使用 `compute_dirty_region_into(...)`，把输出写入长期持有的
`FrameScratch::dirty_region`，并把内部 bounds 工作区写入 `FrameScratch::dirty_region_scratch`。
这样 dirty rectangles 和 dirty-node bounds 的 capacity 可以跨帧复用；内存压力、息屏或切换 app 时再调用
`FrameScratch::release()`。旧的返回值 API 主要用于简单工具和兼容调用点。

`dirty_region_mode_name(...)` 和 `dirty_region_fallback_reason_name(...)` 提供稳定的短名称，
供壳层诊断使用。Win32 验证壳会在增量重绘后把这些信息显示在窗口标题中，交互时可以直接看到 fallback 原因。

`DirtyRegionStatistics` 可累计多次 `DirtyRegionResult` 样本。它会记录 clean frame、dirty-rect
frame、full-frame frame、总 rect 数、总 dirty area 和 fallback reason 次数。它面向审计：
先测出 full-frame fallback 主要来自哪里，再决定是否加入更重的 retained subtree reuse。
它应和 `FrameUpdateStatistics` 配合看：frame-update reason 解释为什么选择 rebuild，dirty-region
reason 解释为什么本来计划局部重绘却仍退回 full frame。

宿主还可以使用 `dirty_region_area(...)`、`dirty_region_area_percent(...)`
和 `dirty_region_should_repaint_incrementally(...)` 判断局部重绘是否仍然划算。面积估计会把裁剪后的
rect 面积直接求和，不扣除重叠部分；这是刻意保守的嵌入式友好成本信号。若估算面积超过宿主阈值，
全帧重绘可能比提交大量局部 flush 更便宜。Win32 验证壳当前使用 70% 阈值，并在因此选择全帧时记录
`DirtyAreaTooLarge`。

## Display Invalidation 诊断

头文件：`src/render_core/display_invalidation.h`

`analyze_display_invalidation(...)` 会报告一组 dirty rectangles 映射到当前 layer tree 和
display commands 后的覆盖情况。它会统计访问/命中的 layer、带 clip/composited 的命中 layer、
访问/命中的 display command。Win32 验证壳会在窗口标题中用 `cmds=命中/访问` 显示最近一次
command 覆盖情况。

这是审计 helper，不是 retained display-list reuse。Compositor 仍会在每个 dirty clip 内重放命令，
但会先丢掉重复或被完全包含的 dirty rectangles，避免同一 repaint 区域被清屏和重放多次。它的价值是让
宿主和测试能看到一次页面交互是否真的缩小了 paint 工作，然后再决定是否值得加入更重的
retained layer/display-command 结构。

## Animation Invalidation

头文件：`src/render_core/animation_invalidation.h`

`compute_animation_dirty_region(...)` / `compute_animation_dirty_region_into(...)` 面向 animation frame。
它们使用上一帧和当前帧的 `StyleOverride` 列表，在当前 layout tree 中找到对应节点，并把节点 subtree
的基础 bounds、上一帧 transform bounds 和当前帧 transform bounds 合并为 dirty rectangles。

这条路径专门避免动画帧因为 root `DomDirtyPaint` 退回全帧。适用范围是当前 D 主线承诺的
paint/compositor 属性：`opacity`、`background-color`、`color` 和 `transform: translate()/scale()/rotate()`。
layout 属性动画仍不逐帧重排；如果动画改变了 DOM 结构或 layout，仍应走普通 dirty-region/full-frame
路径。

宿主可以设置 root 的聚合 `DomDirtyPaint` bit 来调度动画帧，但 animation-only 工作不应把 root
标成 local dirty node。root 的 local dirty bounds 等价于整篇文档；如果同一帧还有脚本/text 工作，
它会吞掉 animation dirty-region 计算带来的局部重绘收益。

CSS `@keyframes` / `animation-*` 使用同一条 timeline 和 dirty-region 路径。第一版子集只从已经解析的
render-tree style 启动动画，采样同一组 paint/compositor 属性的 `from`/`to` declarations，并让
transition 与 keyframe animation 共享 `max_active_animations` 预算。不支持的 keyframe 属性通过
diagnostics 报告并忽略，不触发逐帧 layout。

## 边界

当前核心仍不做：

- retained layout tree 的完整复用。
- retained display-command reuse。
- tiled/scanline renderer。
- 自动线程调度或低功耗策略。

这些分别属于未来 retained-rendering 工作、可选 tiled presentation 或宿主策略。

## 验收

- clean + cached frame 不工作。
- clean + uncached document 触发首帧 full render。
- paint-only dirty 在缓存齐全且 framebuffer 尺寸匹配时复用 render/layout。
- layout/style/text dirty 在新 layout 解析后 framebuffer 尺寸仍匹配时，可比较旧/新 layout
  后增量重绘。
- 缓存缺失、尺寸变化或 viewport 无效时保守 full frame。
- 长时间 frame-loop smoke 会验证 input/timer 每帧消费有上限，且积压排空后能回到 clean cached idle frame。
