# JellyFrame 移植工作指导


本文面向正在把 JellyFrame 移植到 ESP32-S3、RTOS、LVGL 宿主或自定义可穿戴硬件的开发者。它不是浏览器功能说明，而是移植侧的任务书：每个模块需要交付什么、应该如何接入当前核心、如何验收，以及当前核心已经提供了哪些可直接使用的能力。

当前核心已经足够支持第一版“静态资源 + 软件渲染 + RGB565 局部提交 + 触摸/按键输入 + bitmap 字体 + 可选 JerryScript”的开发板 bring-up。尚未具备无完整 framebuffer 的 tiled renderer、生产级复杂文字 shaping、图片解码和网络资源栈。

## 当前可确定的核心契约

这些接口已经存在，可作为移植开发的稳定入口：

- `src/render_core/host.h`：设备能力、资源请求、时钟、frame sink、预算。
- `src/render_core/budget.h`：把 `HostBudgets` 映射到 parser、render/layout/layer、dirty rect 和 scripting 限制。
- `src/render_core/embedded_framebuffer.h`：把核心 RGBA framebuffer 转换到宿主持有的 RGB565、灰度、单色等目标 buffer。
- `src/render_core/frame_scratch.h`：帧级 dirty-region、动画 override 等临时容器复用与显式释放。
- `src/render_core/input.h`：触摸、指针、滚轮、按键、文本输入、焦点导航和激活。
- `src/render_core/text_backend.h`、`src/render_core/bitmap_font.h`：宿主文本测量与 bitmap 字体绘制。
- `src/render_core/document_style.h`、`src/render_core/document_script.h`：外链 CSS 和 classic script 的宿主加载 callback。
- `src/script/jerryscript_runtime.h`：可选 JerryScript runtime、DOM/event/form/timer bridge。
- `src/app_runtime/app_host.h`：`AppRuntimeHost` 和 `AppFrameScratch`，用于 app lifecycle、host completion
  每帧泵动与临时 completion batch 复用。

第一版开发板 port 不应直接调用 Win32、文件系统或桌面壳代码。参考结构是
`ports/embedded_host_demo`，它已经证明核心可在无窗口、无文件、无网络、无 Win32
的情况下串起静态 HTML/CSS、bitmap 字体、输入和 RGB565 提交。

## 职责边界

本文只把移植侧需要完成的工作列为阶段任务。以下内容不属于硬件移植任务：

- 修改 HTML/CSS/DOM/layout/render 核心算法。
- 在 `jellyframe_render_core` 内加入字体文件加载、文件系统、网络或屏幕驱动。
- 重新实现 `jellyframe_font_resource_check`、`jellyframe_font_pack_gen`、`embedded_framebuffer` 或 bitmap font callback。
- 在 ESP32-S3 第一版中实现无完整 framebuffer 的 tiled renderer、复杂文字 shaping、图片解码或网络安全模型。

如果产品必须依赖这些能力，应先回到主线核心规划新里程碑；不要把它们硬塞进板级 port。

## 移植阶段

### P0：Port 骨架

目标：加入可维护的 port 目录，不破坏现有核心构建。

任务要求：

- 新建或整理 `ports/esp32s3-idf/`，保留为独立 ESP-IDF app。
- 新建或整理 `ports/virtual_board/`，保留为桌面性能估算工具。
- 不要覆盖主线根 `CMakeLists.txt`。
- ESP-IDF render component 名称使用 `jellyframe_render_core`。
- 日志 tag、Kconfig menu、README 和 benchmark 输出使用 JellyFrame。

实现方式：

- ESP-IDF app 目录只包含 port 代码、Kconfig、sdkconfig defaults 和 component CMake。
- component CMake 引用主线 `src/render_core/*.cpp`，源码清单必须跟根 CMake 当前的
  `jellyframe_render_core` 保持一致。
- JerryScript 不进入 P0/P1 的 ESP32-S3 component；先证明非脚本管线稳定。

验收：

- 桌面主线仍能配置、构建和测试。
- ESP-IDF app 能执行 `idf.py set-target esp32s3 && idf.py build`。

### P1：设备能力与预算

目标：开发板明确告诉核心“这个设备能承受什么”，避免无限分配或不可控退化。

任务要求：

- 在 port 层填充 `HostDeviceCapabilities`。
- 根据实际屏幕、PSRAM、堆大小和最大连续块设置 `HostBudgets`。
- 所有 parser/layout/layer/dirty/script 入口都通过 `src/render_core/budget.h` 派生 options。
- 当资源或 framebuffer 超预算时，必须降级或跳过，不允许崩溃。

ESP32-S3 初始建议：

```cpp
jellyframe::HostBudgets budgets;
budgets.max_dom_nodes = 1024;
budgets.max_dom_depth = 48;
budgets.max_attributes_per_element = 24;
budgets.max_css_rules = 512;
budgets.max_css_declarations_per_rule = 96;
budgets.max_render_objects = 1024;
budgets.max_layout_boxes = 1024;
budgets.max_layers = 128;
budgets.max_display_commands = 2048;
budgets.max_dirty_rects = 8;
budgets.max_timers = 24;
budgets.max_event_listeners = 192;
budgets.max_animation_callbacks_per_frame = 2;
budgets.max_active_animations = 8;
budgets.animation_frame_rate = 30;
budgets.max_resource_bytes = 128 * 1024;
budgets.max_framebuffer_pixels = width * height;
```

4 MB PSRAM 可以跑 300x300 合成基准，但只适合 bring-up 和简单 UI。8 MB 是实际产品的保守基线；如果计划启用 JerryScript、中文字库、多个页面或资源缓存，优先选 16 MB。

验收：

- 低预算配置能正常截断或降级。
- 资源过大、DOM 过深、CSS 过多、dirty rect 过多时不死循环、不越界、不崩溃。
- 串口输出记录预算值、最大 heap watermark、最大单次可分配块。

### P2：资源包加载

目标：让 HTML/CSS/JS 从 flash、partition、LittleFS/FATFS 或静态数组进入核心。

任务要求：

- 支持 `HostResourceKind::Stylesheet` 和 `HostResourceKind::ClassicScript`。
- 相对路径解析由宿主完成，核心不假设文件系统。
- 每次读取遵守 `max_resource_bytes`。
- 缺失 CSS/script 必须可恢复：CSS 缺失则按默认样式渲染，script 缺失则继续静态页面。
- release 产品建议使用编译期资源 bundle；文件系统只作为开发调试选项。

实现方式：

- 当前 ESP32-S3 bring-up：顶层 `tools/package_app.py` 会校验
  `ports/esp32s3-idf/resources/app/jellyframe.app.json`，并从
  `ports/esp32s3-idf/resources/app` 生成
  `ResourceEntry { url, kind, bytes, size }` 只读表。
- 若使用分区或文件系统，读取到有界 `std::string` 后交给现有 document style/script loader。

验收：

- 静态资源、内联 `<style>`、本地 `<link rel="stylesheet">`、classic `<script>` 均能通过宿主 callback 加载。
- 资源缺失不会阻塞首帧。
- 字体资源检查器可以在桌面扫描同一组资源中的文本与 glyph 覆盖。

### P3：显示与 framebuffer

目标：在开发板上把核心输出提交到真实屏幕。

当前核心能力：

- 核心渲染使用逻辑 RGBA8888 `FrameBuffer`。
- `embedded_framebuffer` 可把 dirty rect 转换为宿主持有的 RGB565、BGR565、RGB332、Gray8 或 1-bit target buffer。
- `flush(Rect)` 由宿主调用屏幕驱动。

任务要求：

- 优先实现 RGB565 target buffer。
- 保留一个持久 `FrameBuffer`，用于 dirty repaint。
- 如内存允许，再保留一个持久 RGB565 target buffer，避免每帧临时分配；internal RAM 紧张时不要把整屏
  RGB565 target 放 internal RAM。
- `flush` 只提交 dirty rectangle，不做整屏无条件刷新。
- 若屏幕驱动 API 要求紧凑行缓冲，而 dirty rect 不是整行，宿主应在栈或静态 scratch buffer 中逐行打包。
- `present`/`flush` 必须是帧同步边界：返回后，JellyFrame 才能安全开始下一帧写入同一 framebuffer 或
  target buffer。

实现方式：

```cpp
jellyframe::EmbeddedFrameBufferTarget target {
    width,
    height,
    jellyframe::EmbeddedPixelFormat::Rgb565,
    reinterpret_cast<std::uint8_t*>(rgb565_pixels),
    rgb565_size_bytes,
    width * sizeof(std::uint16_t),
    true, // ordered_dither，可按产品显示效果关闭
};

jellyframe::EmbeddedFrameBufferSink sink { target, flush_dirty_rect, panel_context };
jellyframe::HostFrameSink frame_sink = jellyframe::embedded_frame_sink(sink);
jellyframe::present_frame(framebuffer, frame_sink, dirty_rects, dirty_count);
```

如果实际屏幕驱动使用异步 DMA，`flush_dirty_rect` 不能简单“发起 DMA 后立即返回”并允许下一帧继续写同一
buffer。必须选择一种策略：

- 在 `flush_dirty_rect` 中等待 panel 的 flush-done/transfer-done 信号；
- 使用双 buffer 或 driver-owned bounce buffer，让返回时 JellyFrame 的 target 已不再被 DMA 读取；
- 在外层 UI loop 维护 `present_in_flight`，只有收到 flush-done 事件后才进入下一轮 render/present。

这和 LVGL 的 draw buffer/flush ready 语义一致：框架可以异步提交，但 draw buffer 的复用必须等驱动明确完成。

验收：

- 纯色背景、边框、文本、按钮、range/select/checkbox 的静态绘制正确。
- dirty rect 面积明显小于整屏时，实际屏幕提交面积也随之减少。
- 旋转、睡眠唤醒、重复 repaint 不产生花屏。

当前不能直接满足的场景：

- 如果设备无法容纳完整 RGBA framebuffer，不能硬接。必须先回核心实现 tiled rendering 或 scanline/tile compositor。
- 如果硬件只支持极小 DMA buffer，宿主可以压缩 RGB565 target，但核心仍需要源 RGBA framebuffer。真正无源 framebuffer 需要新核心工作。

internal RAM 压力处理建议：

- RGBA `FrameBuffer` 优先放 PSRAM；它不是 DMA target，主要由 CPU renderer 读写。
- RGB565 整屏 target 优先放 PSRAM，前提是目标 panel driver 支持从 PSRAM DMA 或能安全读取；否则不要保留整屏
  internal RGB565 target。
- 需要 DMA-capable internal RAM 时，只保留 1-2 个小 strip/tile buffer，例如 8-32 行，按 dirty rect 转换、
  flush、等待完成，再复用。
- render/layout/layer 的 `MonotonicArena` 如果用于 retained tree，就会跨帧保留；如果产品选择每次全量
  rebuild，可在 present 后 reset 这些 arena，但会牺牲 CPU。不要把这类 arena 放 internal RAM，除非测量证明必要。
- 离屏合成 buffer 是 `SoftwareCompositor::render_into` 内部临时对象，函数返回后释放；应通过
  `max_offscreen_pixels` 限制它，避免大 opacity/transform layer 临时吃光 internal heap。
- RGB565/BGR565 屏幕如出现渐变色带，可在 `EmbeddedFrameBufferTarget::ordered_dither` 开启 4x4
  ordered dithering。它只影响最终颜色量化，不解决几何锯齿；圆角和缩放抗锯齿由 render core 在 RGBA
  framebuffer 阶段处理。
- dirty rect 临时数组、completion event 临时数组、资源读取临时 buffer 都应在帧边界后释放或复用为静态小 buffer。
- 主循环建议持有 `FrameScratch frame_scratch; AppFrameScratch app_scratch;`，启动时按预算 reserve。
  计算 dirty region 时优先用 `compute_dirty_region_into(..., frame_scratch.dirty_region,
  &frame_scratch.dirty_region_scratch)`；泵 host completion 时用
  `app_runtime.pump_frame_completions(app_scratch)`。这会复用 dirty bounds、dirty rects、completion batch
  和 accepted list。
- `FrameScratch::release()` / `AppFrameScratch::release()` 可在息屏、切 app、退出当前 app 或内存压力回调中调用。
  这部分可以由 JellyFrame 平台无关层安全释放；真实 DMA buffer、panel draw buffer、strip buffer 是否可释放，
  仍取决于 port 是否已经收到 flush-done/transfer-done。

### P4：文本与中文字库

目标：确保开发者写中文 UI 时能预测字体覆盖、测量和绘制结果。

核心已提供：

- `TextMeasureProvider` 和 `TextPainter` callback 接口。
- `src/render_core/bitmap_font.h` 中的 bitmap font 数据结构、测量 callback 和绘制 callback。
- `jellyframe_font_resource_check`，可扫描 HTML/CSS/JS 使用的非 ASCII 字符、检查字体覆盖、
  估算 bitmap pack 预算并建议字体 profile。
- `jellyframe_font_pack_gen`，可从 BDF 字体子集生成 C++ `BitmapFont` header。
- `src/render_core/text_adapter.h` 中的 `HostTextAdapter`，用于把 LVGL/vendor 测量和绘制 callback
  包装成核心需要的接口，同时不把平台头文件带进 core。

任务要求：

- 选择并记录产品使用的字体 profile、授权源字体、字号、字重和目标 DPI/像素高度。
  `cn-standard` 是中文市场推荐的可复用 profile；flash 紧张的产品应优先
  app-specific subset，全球化产品应按市场选择字体子集。
- 在板级或应用构建流程中生成 bitmap font pack；生产固件不得依赖运行时矢量字体 rasterizer。
- 把生成的 `BitmapFont` C++ header 编译进 ESP-IDF app、RTOS app 或板级 BSP。
- 在 port 层创建持久 `BitmapFontContext`，测量和绘制必须使用同一份 glyph metrics。
- 把 `bitmap_font_measure_callback` 接入 `LayoutEngine`，把 `bitmap_font_paint_callback` 接入 `SoftwareCompositor`。
- 字体回调内部不得堆分配，不得访问文件系统，不得阻塞等待外设。
- 缺字必须显示可见 fallback glyph，并保持稳定 advance，不允许破坏布局。
- 桌面构建或发布前运行字体覆盖检查；缺字应让构建失败，或至少产生明确警告。

实现方式：

1. 在桌面构建机上运行：

   ```text
   jellyframe_font_resource_check --emit-used-chars used_chars.txt app.html app.css app.js
   jellyframe_font_resource_check --font-budget 16x16 app.html app.css app.js

   根据输出的 font profile，在 `tiny`、`app-subset-cn`、`cn-standard` 和
   按市场划分的 global packs 之间选择。
   ```

2. 用板级项目选择的离线工具，从授权源字体和 `used_chars.txt` 生成 BDF 或等价 bitmap glyph 数据。
3. 在桌面构建机上运行：

   ```text
   jellyframe_font_pack_gen --bdf app_font.bdf --chars used_chars.txt --output app_font.h --name app_font
   ```

   生成器会输出 requested/emitted glyph 数、row bytes、glyph table 估算 bytes 和总估算 bytes。
   这些数字应记录到 port 文档中。

4. 将 `app_font.h` 放入 port 或应用资源目录，并加入 ESP-IDF/RTOS 构建。
5. 在创建 layout engine 和 compositor 时注入同一个 font context：

   ```cpp
   static jellyframe::BitmapFontContext font_context{&app_font, 1};

   jellyframe::TextMeasureProvider measure{
       jellyframe::bitmap_font_measure_callback,
       &font_context,
   };

   jellyframe::TextPainter painter{
       jellyframe::bitmap_font_paint_callback,
       &font_context,
   };
   ```

验收：

- 中文、数字、符号在按钮、标题、列表和输入控件中不裁切。
- 加粗至少能通过选中粗体 glyph 或近似加粗策略表现。
- 字体缺字报告能在桌面构建阶段失败或警告。
- 同一页面在桌面 bitmap font smoke 和开发板屏幕上换行位置基本一致。
- 字体包大小、覆盖字符数、字号和源字体许可证记录在 port 文档中。

暂不建议：

- 在 ESP32-S3 第一版上运行完整矢量字体 rasterizer。
- 在核心里加入字体文件加载器。
- 为第一版实现复杂 shaping。阿拉伯文、印地文字等复杂文字应明确列为后续能力。

### P5：输入与交互

目标：把真实硬件输入转换为平台无关 DOM/input 事件。

任务要求：

- 触摸屏：映射为 `pointer_down`、`pointer_move`、`pointer_up`。
- 表冠：根据产品交互选择映射为 `wheel` 或 `focus_next/focus_previous`。
- OK/确认键：映射为 `activate_focused`。
- Back 键：由宿主处理页面导航；在文本框内可映射为 `KeyCode::Backspace`。
- 软键盘/IME：通过 `text_input(utf8)` 发送 UTF-8。
- 每个硬件事件处理时间必须有上限；不得在 ISR 中直接跑布局或脚本。

实现方式：

- ISR 或驱动回调只入队轻量事件。
- UI task 每 tick 取有限个事件，调用 `InputController`。
- 输入导致 DOM dirty 后，再进入有限重建/重绘路径。
- ESP32-S3 参考工程现在包含 `BoardInputQueue` 固定容量示例。队列满时丢弃新事件并记录
  `dropped_count()`。产品 port 可以调整事件结构，但必须保持内存有界、每帧派发有界，
  且不得在中断上下文直接执行 layout 或脚本。

验收：

- button、checkbox、radio、range、select、text input 可用。
- 事件委托场景可用：`addEventListener` 绑定父节点后点击子控件能冒泡。
- `disabled` 控件不能被激活；`hidden` 元素不可见且不参与交互。
- 快速连续触摸或表冠旋转不会造成事件队列无限增长。

### P6：运行循环与增量更新

目标：形成稳定、可测的嵌入式 UI frame loop。

任务要求：

- 首帧执行完整 pipeline：HTML/CSS -> DOM -> style -> render tree -> layout -> layer -> framebuffer。
- 非结构性变化优先走 dirty rect repaint。
- 结构性 DOM 变化可以保守全 viewport 重建。
- 每帧限制 timer callback、输入事件和重绘区域数量。
- 低功耗息屏时停止或降低 repaint/timer pumping。

推荐循环：

```text
load resources once
build first document and style
render first frame
present dirty/full frame
wait/certify present completion before reusing frame buffers

loop:
  if display present is still in flight:
      sleep or process non-render work that cannot touch frame buffers
      continue
  frame_scratch.begin_frame()
  app_scratch.begin_frame()
  poll bounded hardware events
  dispatch input through InputController
  pump bounded timers if scripting is enabled
  pump bounded host completions through app_scratch
  if tree/style/layout dirty:
      rebuild conservative pipeline
  else if paint dirty:
      compute dirty rects
  repaint dirty/full regions
  present through HostFrameSink
  frame_scratch.end_frame()
  app_scratch.end_frame()
  mark display present in flight if panel DMA is asynchronous
  sleep until next tick or hardware event
```

验收：

- 静态 UI 首帧可显示。
- 控件状态变化不强制每次清空整屏。
- timer 驱动时钟/计时器示例不会越跑越慢。
- 所有队列都有上限。
- display flush 未完成时不会开始下一帧写同一 framebuffer/target buffer；异步 DMA 不撕裂、不花屏。
- ESP32-S3 bring-up 当前在 `app_main` 中验证 P4/P5/P6，并把默认主任务栈提高到 32 KB。
  产品 port 应将其移动到可测量的 UI task，复用持久 buffer，并在目标板允许时继续压低栈使用。

### P7：JerryScript 接入

目标：在非脚本管线稳定后启用小型 JavaScript app。

前置条件：

- P1-P6 已通过。
- 字体和 framebuffer 路径稳定。
- 资源加载可加载 classic script。
- timer pumping 有每 tick callback 上限。
- 出错日志能打印 script exception。

任务要求：

- JerryScript 作为可选 component，不进入 `jellyframe_render_core`。
- heap、timer、listener 数量必须按 `HostBudgets` 限制。
- script 执行不得发生在 ISR 或显示驱动 callback 中。
- 页面脚本仅承诺 classic script 子集，不承诺 ES modules、fetch、Web APIs、CSSOM 动态全量能力。

验收：

- `calculator`、`timer`、`clock` 这类小型应用能在开发板上响应触摸/按钮。
- `setTimeout`/`setInterval` 可由宿主泵动。
- DOM mutation 后能触发重绘。
- 脚本异常不会崩溃 UI task。

### P8：诊断、基准和发布准入

目标：让每个开发板 port 有可复现的性能和内存数据。

任务要求：

- 输出每阶段耗时：parse、style/render tree、layout、layer、flatten、render、present。
- 输出 heap free、minimum heap、largest free block、PSRAM free。
- 输出 dirty rect count、dirty area、flush count、flush bytes。
- 提供 QEMU 或 virtual board benchmark，但最终必须补真实硬件数据。
- release build 可关闭详细诊断。

已有 QEMU 实验结论：

- ESP-IDF `v5.3.1` + QEMU `esp_develop_9.2.2_20260417` 可跑 ESP32-S3 PSRAM 基准。
- Octal PSRAM 下 4M、8M、16M、32M 均完成 300x300、40 cards、20 iterations。
- 4 MB 是最低可行 bring-up；8 MB 是较合理基线；16 MB 更适合启用 JerryScript、中文字库和资源缓存。
- QEMU timing 只用于趋势和容量验证，不替代真实芯片 FPS/延迟。

发布准入：

- 主线桌面测试通过。
- port 的 ESP-IDF build 通过。
- 至少一个静态页面、一个控件页面、一个 timer/script 页面在目标板或 QEMU 上通过。
- 文档记录硬件型号、PSRAM、屏幕接口、分辨率、字体包大小、资源大小、平均帧耗时和最大 heap watermark。

## 当前前置工作判断

第一版 ESP32-S3 bring-up 不再需要等待核心新增接口。当前主线已经具备静态资源、预算、RGB565 提交、dirty rect、bitmap font、输入事件和可选 JerryScript 的入口。硬件移植侧后续重点是补齐真实屏幕驱动、输入驱动、字体资源、运行循环策略和真实硬件数据。

以下能力如果被产品目标强制要求，则必须先规划核心侧开发：

- 无完整 RGBA framebuffer 的 tiled renderer。
- 图片解码和 `object-fit` 的真实图像绘制。
- 更细粒度 layer/display-command invalidation。
- 复杂文字 shaping。
- 网络资源加载和安全模型。
- 更少堆碎片的 arena allocator 与 small-vector attributes。

## 建议合并顺序

1. 在 ESP-IDF app 中按目标硬件校准 `HostBudgets` 和 `HostDeviceCapabilities`。
2. 确认资源包生成流程覆盖目标 app 的 HTML/CSS/classic script。
3. 接入真实屏幕 `flush(Rect)`，验证 RGB565、dirty rect 和睡眠唤醒。
4. 接入 bitmap font pack，完成中文字体覆盖检查和板端显示 smoke。
5. 接入触摸/按键/表冠事件队列。
6. 完成 UI task 运行循环：输入、timer、dirty rebuild/repaint、present 和低功耗节流。
7. 跑真实硬件基准并更新 port 文档。
8. 非脚本路径稳定后，再启用 JerryScript component。
