# 运行循环与增量更新契约

日期：2026-06-16

本文定义 JellyFrame 主线核心建议的宿主运行循环。它只描述硬件无关契约：输入、timer、dirty flags、重建、重绘和提交如何排序。真实线程、ISR、屏幕驱动、电源管理和 RTOS 调度由宿主负责。

## 推荐顺序

首帧：

1. 加载本地资源。
2. 解析 HTML/CSS。
3. 可选：绑定并执行 classic scripts。
4. 构建 render tree、layout tree、layer tree。
5. 渲染完整 framebuffer。
6. 通过 `HostFrameSink` 或 `embedded_framebuffer` 提交 full dirty rect。
7. 清理 DOM dirty flags。

循环帧：

1. 从宿主队列取有限个输入事件。
2. 通过 `InputController` 派发 pointer/wheel/key/text/focus 操作。
3. 如果启用 JerryScript，泵动有限个 timer callback。
4. 读取根节点 `subtree_dirty_flags(document)`。
5. 填充 `FramePipelineCacheState`，调用 `make_frame_update_state(...)`，再调用
   `plan_frame_update(...)` 决定更新路径。
6. 按计划复用现有 layout/layer，或重建管线。
7. 新 layout 已知后，用解析出的内容高度调用 `plan_frame_repaint(...)`，确认现有
   framebuffer 尺寸是否仍匹配。
8. 用 `compute_dirty_rects(...)` 生成 dirty rectangles，或保守全帧。
9. 调用 `SoftwareCompositor::render_into(...)` 或完整 `render(...)`。
10. 通过 `HostFrameSink` 提交 dirty rectangles。
11. 清理 DOM dirty flags。

宿主可以把这些步骤放进一个 UI task、桌面消息循环或测试壳中，但不应在 ISR 或屏幕 flush callback 内执行脚本、layout 或渲染。

## `plan_frame_loop`

头文件：`src/core/frame_loop.h`

`plan_frame_loop_work(...)` 是一个很薄的宿主 UI task 辅助函数。它不拥有输入队列，也不拥有
timer 队列。宿主通过 `FrameLoopPendingWork` 告诉核心当前有多少待处理输入事件和到期 timer
callback，然后得到一个有界的 `FrameLoopWorkPlan`：

- `input_events_to_dispatch`：本帧最多取多少个宿主输入事件。
- `timer_callbacks_to_pump`：本帧最多泵动多少个脚本 timer callback。
- `has_more_input_events` / `has_more_timer_callbacks`：是否还有积压，宿主可据此继续调度下一帧或保持
  UI task 唤醒。

`FrameLoopOptions` 保存每帧上限。上限为 0 是合法的，表示宿主刻意暂停该类工作，例如息屏低功耗节流。
这个 helper 不会丢弃工作，只告诉宿主本帧应消费多少。宿主可以用
`frame_loop_options_from_budgets(...)` 从 `HostBudgets` 派生这些上限。

`plan_frame_loop(...)` 会把上述有界工作计划和 `plan_frame_update(...)` 合成一次调用，适合希望统一规划的宿主。
它仍然不派发输入、不泵动 timer、不修改 DOM、不重建 layout，也不提交像素。

## `plan_frame_update`

头文件：`src/core/frame_update.h`

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
- `FrameDirtyRectMode::CurrentLayout`：dirty rect 可从当前 layout 计算，适合 paint-only 变化。
- `FrameDirtyRectMode::PreviousAndCurrentLayout`：重建后比较旧/新 layout，适合文本、样式或布局变化的增量重绘。
- `FrameDirtyRectMode::FullFrame`：缺少缓存、尺寸不匹配或 viewport 无效时保守整帧。

`plan_frame_repaint(...)` 是重建 layout 后使用的第二阶段检查。它只在现有
framebuffer 尺寸仍匹配解析后的目标高度时保留 `PreviousAndCurrentLayout` 或
`CurrentLayout`。如果文本、样式或布局变化导致内容变高或变矮，宿主必须 resize 或重建
framebuffer，并执行 full frame repaint。

## Dirty Flags 语义

- `DomDirtyPaint`：控件值、选择状态、焦点类视觉变化等。若 framebuffer 和 layout cache 可用，可走 `RepaintExisting`。
- 不含 `DomDirtyTree` 的 `DomDirtyText` / `DomDirtyLayout` / `DomDirtyStyle` /
  `DomDirtyAttributes`：需要重建 render/layout/layer，但在 framebuffer 尺寸不变时可以尝试
  `PreviousAndCurrentLayout` dirty rect。
- `DomDirtyTree`：结构变化。Planner 直接使用 `FullFrame`，且不保留上一棵 layout tree，
  因为当前 dirty-region 逻辑会保守退回整 viewport/content rect。后续 M9 可以继续细化。

同值 `textContent`、未变化 attribute 等不应制造 dirty flags。

## Dirty Region 诊断

头文件：`src/core/dirty_region.h`

`compute_dirty_rects(...)` 仍保留为只需要矩形的宿主使用的兼容 API。M9 新增
`compute_dirty_region(...)`，返回同样的 rectangles，并额外给出：

- `DirtyRegionMode::Clean`：无需重绘。
- `DirtyRegionMode::DirtyRects`：已生成有界局部矩形。
- `DirtyRegionMode::FullFrame`：核心选择保守 full-frame fallback。

`DirtyRegionFallbackReason` 会说明为什么退回 full-frame：viewport 无效、缺少 previous/current
layout、结构性 tree dirty、找不到 dirty node bounds，或局部 rect 被裁剪后全部为空。这个接口是给宿主和测试观察边界用的诊断契约；
它不表示 retained subtree reuse 或 display-command 级 invalidation 已经完整实现。

`dirty_region_mode_name(...)` 和 `dirty_region_fallback_reason_name(...)` 提供稳定的短名称，
供壳层诊断使用。Win32 验证壳会在增量重绘后把这些信息显示在窗口标题中，交互时可以直接看到 fallback 原因。

`DirtyRegionStatistics` 可累计多次 `DirtyRegionResult` 样本。它会记录 clean frame、dirty-rect
frame、full-frame frame、总 rect 数、总 dirty area 和 fallback reason 次数。它面向 M9 审计：
先测出 full-frame fallback 主要来自哪里，再决定是否加入更重的 retained subtree reuse。

宿主还可以使用 `dirty_region_area(...)`、`dirty_region_area_percent(...)`
和 `dirty_region_should_repaint_incrementally(...)` 判断局部重绘是否仍然划算。面积估计会把裁剪后的
rect 面积直接求和，不扣除重叠部分；这是刻意保守的嵌入式友好成本信号。若估算面积超过宿主阈值，
全帧重绘可能比提交大量局部 flush 更便宜。Win32 验证壳当前使用 70% 阈值，并在因此选择全帧时记录
`DirtyAreaTooLarge`。

## 边界

当前核心仍不做：

- retained layout tree 的完整复用。
- display-command 级别 invalidation。
- tiled/scanline renderer。
- 自动线程调度或低功耗策略。

这些分别属于 M9、M13 或宿主策略。

## 验收

- clean + cached frame 不工作。
- clean + uncached document 触发首帧 full render。
- paint-only dirty 在缓存齐全且 framebuffer 尺寸匹配时复用 render/layout。
- layout/style/text dirty 在新 layout 解析后 framebuffer 尺寸仍匹配时，可比较旧/新 layout
  后增量重绘。
- 缓存缺失、尺寸变化或 viewport 无效时保守 full frame。
- 长时间 frame-loop smoke 会验证 input/timer 每帧消费有上限，且积压排空后能回到 clean cached idle frame。
