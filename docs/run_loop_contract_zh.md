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
7. 用 `compute_dirty_rects(...)` 生成 dirty rectangles，或保守全帧。
8. 调用 `SoftwareCompositor::render_into(...)` 或完整 `render(...)`。
9. 通过 `HostFrameSink` 提交 dirty rectangles。
10. 清理 DOM dirty flags。

宿主可以把这些步骤放进一个 UI task、桌面消息循环或测试壳中，但不应在 ISR 或屏幕 flush callback 内执行脚本、layout 或渲染。

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

## Dirty Flags 语义

- `DomDirtyPaint`：控件值、选择状态、焦点类视觉变化等。若 framebuffer 和 layout cache 可用，可走 `RepaintExisting`。
- `DomDirtyText` / `DomDirtyLayout` / `DomDirtyStyle` / `DomDirtyAttributes`：需要重建 render/layout/layer，但在 framebuffer 尺寸不变时可以尝试 `PreviousAndCurrentLayout` dirty rect。
- `DomDirtyTree`：结构变化。当前 `compute_dirty_rects` 会保守退回整 viewport/content rect。后续 M9 可以继续细化。

同值 `textContent`、未变化 attribute 等不应制造 dirty flags。

## 边界

当前 M8 不做：

- retained layout tree 的完整复用。
- display-command 级别 invalidation。
- tiled/scanline renderer。
- 自动线程调度或低功耗策略。

这些分别属于 M9、M13 或宿主策略。

## 验收

- clean + cached frame 不工作。
- clean + uncached document 触发首帧 full render。
- paint-only dirty 在缓存齐全且 framebuffer 尺寸匹配时复用 render/layout。
- layout/style/text dirty 在 framebuffer 尺寸匹配时可比较旧/新 layout 后增量重绘。
- 缓存缺失、尺寸变化或 viewport 无效时保守 full frame。
