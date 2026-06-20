# 嵌入式 HAL API


这份文档是 ESP32-S3、RTOS host 或具体开发板需要实现的接口清单。`jellyframe_render_core`
保持平台无关；所有真实 I/O 都由硬件程序负责，再通过小型 C++ struct 和 callback 调用核心。

图片/音频/轻量视频、网络数据请求和安装式 bundle 的可选服务形状见
`host_optional_services_zh.md`。本文只保留总 checklist 和移植建议。

## 必需运行循环

最小嵌入式 host 应执行以下循环：

1. 从 flash、partition、ROM bundle 或宿主存储加载 HTML/CSS/script 字节。
2. 构建 DOM、style、render、layout 和 layer tree。
3. 渲染到持久 `FrameBuffer`。
4. 通过 `HostFrameSink` 或 `embedded_framebuffer` 提交 dirty rectangles，并把这一步当作本帧的显示同步边界。
5. 把硬件输入转换成 `InputController` 调用。
6. 如果启用 scripting，泵动 JerryScript timers。
7. 如果 DOM dirty flags 发生变化，重建简化管线并重绘。

JellyFrame 不应把屏幕刷新当作与渲染管线无关的后台进程。渲染一帧、提交 dirty rectangles、等待
屏幕驱动/DMA 完成或确认缓冲区已安全移交，这三步属于同一个 frame transaction。宿主可以异步使用
LCD DMA，但在 DMA 仍读取同一块 framebuffer/转换 buffer 时，UI task 不得开始下一帧写入这些 buffer。
可选实现方式有三种：

- `present()` 阻塞到屏幕 driver flush 完成，最简单，适合低 fps 或小 dirty rect；
- `present()` 把像素复制到 driver-owned DMA buffer 后立即返回，JellyFrame buffer 可复用；
- UI task 记录 `present_in_flight`，只在 panel driver 的 flush-done 回调后进入下一轮 render。

这与 LVGL 的 display flush 模型一致：flush callback 提交像素，driver 完成后通知 ready/wait，框架再继续安全复用 draw buffer。

## 设备能力 API

头文件：`src/render_core/host.h`

```cpp
struct HostDeviceCapabilities {
    HostDisplayCapabilities display;
    HostInputCapabilities input;
    HostMemoryCapabilities memory;
    HostAsyncCapabilities async;
    HostMediaCapabilities media;
    HostNetworkCapabilities network;
    HostAppBundleCapabilities app_bundles;
    HostBudgets budgets;
    bool has_monotonic_clock;
    bool has_filesystem;
    bool has_network;
};
```

开发板 port 应在 bring-up 时填充一次，并让 app host 可以读取。当前字段是描述性契约，
不是强制运行时 registry：

- `display`：宽高、DPI、首选像素格式、是否支持局部提交、完整 framebuffer 是否放得下；
- `input`：touch、pointer、wheel、crown、focus buttons、keyboard 和 UTF-8 text input 是否可用；
- `memory`：总 heap、最大单次分配和建议 framebuffer 预算；
- `async`：宿主是否能把解码、网络、安装等慢任务放到 UI task 外执行，以及每帧最多回收多少完成事件；
- `media`：可选图片/音频/轻量视频能力、首选解码输出格式和尺寸/缓冲上限；
- `network`：可选 runtime data fetch 能力、请求/响应上限，以及是否允许远程页面资源；
- `app_bundles`：可选第三方 flash bundle 安装能力、完整性校验和安装数量/大小上限；
- `budgets`：DOM/CSS/display-list/timer/listener/resource 限制；
- monotonic time、filesystem、network 等服务 flags。

ESP32-S3 手表起步建议：

- `preferred_pixel_format = HostPixelFormat::Rgb565`；
- `supports_partial_present = true`；
- 只有当 RAM/PSRAM 能同时容纳目标 buffer 和核心工作内存时，才设 `has_full_framebuffer = true`；
- 按开发板实际硬件设置 `touch`、`crown`/`focus_buttons`，或二者都启用；
- 除非产品宿主明确向 app 暴露有界网络/数据层，否则保持 `has_network = false`；
- 如果启用网络，也应保持 `network.allows_remote_page_resources = false`，只允许 app runtime data API；
- MP3/小图/MJPEG 可以声明为可选 host service；H.264 或高分辨率视频在 ESP32-S3 上仍应默认关闭，
  只在目标 profile 明确标记实验能力时开启。

## 异步工作 API

JellyFrame 的 UI/main task 拥有 DOM、script、layout、layer 和 framebuffer。任何可能阻塞的工作都不应
在该 task 内同步执行，包括：

- flash 文件遍历、大资源读取和第三方 app 安装；
- 网络请求、DNS/TLS、HTTP body 读取；
- 图片、音频、视频解码；
- 大字体/资源表校验。

推荐宿主提供一个很薄的异步队列：

```text
submit(kind, request, budget, priority) -> job_id
cancel(job_id)
pump_completions(max_events) -> completion events
```

completion event 只能在 UI/main task 的帧边界被消费。事件里应包含 job id、状态、资源句柄或错误码；
不要让 worker 线程直接改 DOM、执行 JS、重建 layout 或写 framebuffer。这样能保证第三方 app 发起
网络请求、播放音频或安装包校验时，系统 app/启动器和当前页面仍能响应输入。

具体 request、completion、surface/audio/fetch/bundle handle 生命周期见 `host_optional_services_zh.md`。

最小 ESP32-S3 策略：

- 一个低优先级 worker task 处理图片 decode、包校验和小型文件 I/O；
- 音频播放使用 host-owned audio pipeline，UI 只拿播放句柄和状态事件；
- 网络 request 使用独立 task，并设置最大并发数、最大响应字节和超时；
- 每帧只回收有限个 completion events，例如 2-4 个，避免大量回调挤爆一帧。

## 媒体与解码 API

ESP32-S3 解码实验包给出的方向可以接受，但要明确边界：

- GMF MP3 bench 在 QEMU 中约 27x real-time，heap 余量稳定；适合作为可选音频播放 host service。
- GMF video bench 实际是 MJPEG -> RGB565，240x240、30 帧样本约 46-49fps，输出 buffer 约 115 KiB。
  它适合作为小尺寸图片/轻量动态图解码能力的依据。
- 2026-06-20 的 H.264 复测使用正确 ESP32-S3 QEMU 9.2.2 和 Octal PSRAM 参数后已能跑通：
  320x192 baseline、4 帧样本在 8/16/32MB PSRAM 下约 14.7-16.5fps，约 0.49-0.55x real-time。
  这足以证明“可实验”，但还不足以作为默认实时视频能力。

因此当前建议：

- **图片 decode**：可选加入。输入来自本地 package 或 future bundle；输出 RGB565/RGBA surface 句柄。
  必须有最大宽高、最大 decoded bytes、最大并发数；大图应在打包期缩放或拒绝。
- **音频 playback**：可选加入。核心/JS 只控制 play/pause/stop/volume 和接收 ended/error 事件；
  PCM buffer、I2S、codec、GMF/ADF pipeline 由宿主持有。
- **视频 decode**：只作为实验性 host service。第一阶段优先支持低分辨率 MJPEG frame provider；
  H.264 可以作为 `supports_h264` 标记下的可选 frame decode profile，但不承诺 `<video>`，
  不进入常规 layout 必需能力，也不列入默认 ESP32-S3 profile。
- **图像作为页面资源**：可以先让 `<img>`/CSS background 使用固定占位盒；接入 image decode 后再替换为真实 surface。

worker 产出的 decoded surface 必须受宿主资源表/缓存管理。UI task 只能引用句柄并在下一帧 dirty repaint；
不要把大块像素复制进 DOM 或 JS 对象。

## 网络与安装 API

JellyFrame 的包资源加载继续保持本地、确定性、禁止远程页面资源。即使启用网络，也只表示 app
可以请求运行时数据：

- 允许：天气 API、设备账号服务、同步小 JSON、下载由系统安装器验证的 app bundle。
- 禁止：直接从网络加载 HTML/CSS/script/image 并参与页面资源解析，除非未来专门扩展安全模型。

第三方 flash bundle 安装应由系统壳或 app manager 完成，不由当前 app 的 JS 直接挂载：

1. 下载或接收 `.jfapp` 到 staging 区。
2. 校验 manifest、版本、目标设备、预算、hash/签名。
3. 在非活动 app 上下文中写入 bundle store。
4. 原子提交资源表索引。
5. 通过事件通知启动器刷新 app 列表。

安装、删除和升级必须可取消或可恢复。任何一步失败都不能破坏当前可启动的 app 表。

## 资源 API

头文件：`src/render_core/host.h`

```cpp
using HostResourceLoadCallback = bool (*)(const HostResourceRequest& request,
                                          std::string& output,
                                          void* context);
```

宿主应支持：

- `HostResourceKind::Stylesheet`
- `HostResourceKind::ClassicScript`
- 后续：`Image`、`Font`、`Other`

ESP32-S3 映射：

- 按 `base_url` 解析相对 `url`。
- 从 flash、LittleFS/FATFS、嵌入式数组或 OTA partition 读取。
- 遵守 `HostBudgets::max_resource_bytes`。
- 可选资源缺失时返回 `false`。

core 不要求网络。

## 时钟和 Timer

头文件：`src/render_core/host.h`

```cpp
struct HostClock {
    std::uint64_t (*now_ms)(void* context);
    void* context;
};
```

宿主拥有 wall-clock 或 monotonic time。JerryScript timers 由宿主泵动：

```cpp
runtime.set_host_time_ms(now_ms);
runtime.pump_timers(now_ms, max_callbacks);
```

ESP32-S3 映射：

- 使用 `esp_timer_get_time() / 1000`、FreeRTOS tick 转换或低功耗 monotonic counter；
- 每帧限制 callbacks 数量，避免 UI 卡顿；
- 低功耗息屏时可延后 timer pumping。

## 显示 API

头文件：

- `src/render_core/software_renderer.h`
- `src/render_core/host.h`
- `src/render_core/embedded_framebuffer.h`

核心 framebuffer view：

```cpp
struct HostFrameBufferView {
    int width;
    int height;
    int stride_pixels;
    const Color* pixels; // logical RGBA8888 pixels
};

struct HostFrameSink {
    bool (*present)(const HostFrameBufferView& frame,
                    const Rect* dirty_rects,
                    std::size_t dirty_rect_count,
                    void* context);
    void* context;
};
```

已提供 adapter：

```cpp
EmbeddedFrameBufferSink sink;
HostFrameSink frame_sink = embedded_frame_sink(sink);
```

支持 target 格式：

- `Rgba8888`
- `Bgra8888`
- `Rgb565`
- `Bgr565`
- `Rgb332`
- `Gray8`
- `Mono1Msb`
- `Mono1Lsb`

ESP32-S3 映射：

- TFT/OLED 优先使用 RGB565。
- RAM 允许时保留一个持久 target buffer。
- 使用 dirty rectangles 限制 SPI/8080/DMA flush 区域。
- 如果完整 framebuffer 放不下，后续需要实现 tiled host sink；当前 adapter 预期调用方持有 target buffer。

宿主需要实现：

```cpp
using EmbeddedFlushCallback = bool (*)(Rect dirty_rect, void* context);
```

`flush` 应只把 dirty rectangle 发送给屏幕驱动。

`HostFrameSink::present` 的返回语义非常重要：返回 `true` 表示调用方可以再次写入或转换同一帧相关
buffer。如果底层 SPI/8080/RGB panel flush 是异步 DMA，宿主必须在 `present` 内等待完成、复制到
不会被下一帧覆盖的 DMA buffer，或者在外层 frame loop 中等待 flush-done 事件后再启动下一帧。
不要让 render task 和 panel DMA 同时访问同一块仍在传输的 RGB565/Framebuffer 内存。

`embedded_framebuffer` 是便利用 adapter：它把 RGBA8888 `FrameBuffer` 的 dirty rect 转换到
调用方提供的完整 target buffer，再调用 `flush(Rect)`。它不分配、不持有、不调度 DMA，也不知道 flush
何时完成。因此：

- 如果完整 RGB565 target buffer 放在 internal RAM 会挤爆系统，不要使用整屏 internal target；
- 如果 PSRAM 可被屏幕 DMA 直接读取，可把持久 RGB565 target 放 PSRAM，并在 flush 完成前保持不写；
- 如果屏幕只能从 internal DMA-capable RAM 读，推荐写自定义 `HostFrameSink`：按 dirty rect 分行或分 tile
  从 RGBA framebuffer 转换到一个很小的 internal DMA scratch buffer，提交并等待该 strip 完成，再处理下一段；
- 如果连 RGBA framebuffer 本身也放不下，当前核心还不能满足，需要先实现 tiled/scanline compositor。

这里的 LVGL 是可选项。可以复用 LVGL 或厂商 BSP 初始化屏幕、触摸控制器、背光，
也可以把 dirty rectangle 转交给厂商 flush primitive；但不建议把 JellyFrame 节点翻译成
LVGL widgets 作为主渲染器。核心 pipeline 和 framebuffer 路径应保持权威，只在最终 I/O
hooks 处适配。

## 输入 API

头文件：`src/render_core/input.h`

触摸或指针：

```cpp
input.pointer_move(PointerInput{x, y, ...});
input.pointer_down(PointerInput{x, y, Primary, buttons});
input.pointer_up(PointerInput{x, y, Primary, buttons});
```

滚轮或表冠：

```cpp
input.wheel(WheelInput{x, y, delta_x, delta_y});
```

文本和按键：

```cpp
input.text_input("UTF-8");
input.key_down(KeyInput{KeyCode::Backspace});
```

只有按键/表冠时的焦点导航：

```cpp
input.focus_next();
input.focus_previous();
input.activate_focused();
```

ESP32-S3 映射：

- 电容触摸：转成屏幕坐标下的 pointer down/move/up。
- 表冠顺/逆时针：应用控件导航用 `focus_next()` / `focus_previous()`，页面滚动用 `wheel()`。
- 表冠按下或 OK 键：`activate_focused()`。
- Back 键：应用自定义导航，或对文本框发送 `KeyCode::Backspace`。
- 屏幕键盘/IME：通过 `text_input()` 发送 UTF-8。

core 负责 hit testing、focus、activation、DOM event dispatch 和基础表单控件状态更新。

## 文本 API

头文件：

- `src/render_core/text_backend.h`
- `src/render_core/software_renderer.h`

测量：

```cpp
using TextMeasureCallback = bool (*)(const std::string& text,
                                     int font_size,
                                     int font_weight,
                                     TextMetrics* metrics,
                                     void* context);
```

绘制：

```cpp
using TextPaintCallback = bool (*)(FrameBuffer& target,
                                   Rect rect,
                                   Color color,
                                   const std::string& text,
                                   int font_size,
                                   int font_weight,
                                   TextCommandAlign align,
                                   bool single_line,
                                   void* context);
```

ESP32-S3 映射：

- 正式 UI 使用一个静态嵌入式 font pack。
- 编译期从授权矢量字体生成该 font pack。
- 测量和绘制使用同一份 glyph metrics。
- 回调内部避免堆分配。
- 中文产品对子集化常用 app 字符，并用 `jellyframe_font_resource_check --font-coverage` 检查覆盖。
- 用字体资源检查输出的 profile 在 `tiny`、中文 app-specific subset、`cn-standard` 和按市场划分的
  global packs 之间选择。`cn-standard` 表示 ASCII + 常用符号 + GB2312 一级汉字，它是中文市场预设，
  不是全球默认字体。

矢量字体在高配目标上可行，但 ESP32-S3 默认路线应是离线 rasterize 后的 bitmap glyph。

core 现在提供 `src/render_core/bitmap_font.h` 支撑这条默认路线。开发板 port 可以把生成的 glyph
数组暴露为 `BitmapFont`，再把 `bitmap_font_measure_callback` 接入 `LayoutEngine`，
把 `bitmap_font_paint_callback` 接入 `SoftwareCompositor`。

桌面生成器接受 BDF 输入：

```text
jellyframe_font_pack_gen --bdf font.bdf --chars used_chars.txt --output font_pack.h --name app_font
```

先用 `jellyframe_font_resource_check --emit-used-chars` 收集字符，再用你偏好的离线工具链从授权源字体生成 BDF。

## 平台无关 Bring-Up 示例

`ports/embedded_host_demo` 是当前核心侧的开发板 bring-up 参考。它刻意避开 Win32、
文件、网络和硬件 I/O：

- 静态 HTML 和 CSS 直接编译进 executable；
- `BitmapFont` 提供测量和绘制 callbacks；
- `InputController::focus_next()` 和 `activate_focused()` 验证按键/表冠式交互；
- `embedded_frame_sink()` 把软件 RGBA frame 转换成 RGB565；
- 极小的 `flush(Rect)` callback 记录真实屏幕驱动应收到的 dirty area。

桌面运行：

```text
ports/embedded_host_demo
```

输出应包含一次 flush、一次 button click、已勾选 checkbox、已变化 select value，以及非零前景像素。
开发板 port 应保持同样结构，只替换静态资源、字体包、输入来源和 framebuffer flush callback。

## Budgets

头文件：`src/render_core/host.h`

```cpp
struct HostBudgets {
    std::size_t max_dom_nodes;
    std::size_t max_dom_depth;
    std::size_t max_attributes_per_element;
    std::size_t max_css_rules;
    std::size_t max_css_declarations_per_rule;
    std::size_t max_render_objects;
    std::size_t max_layout_boxes;
    std::size_t max_layers;
    std::size_t max_display_commands;
    std::size_t max_dirty_rects;
    std::size_t max_timers;
    std::size_t max_detached_dom_nodes;
    std::size_t max_input_events_per_frame;
    std::size_t max_timer_callbacks_per_frame;
    std::size_t max_animation_callbacks_per_frame;
    std::size_t max_active_animations;
    std::size_t animation_frame_rate;
    std::size_t max_event_listeners;
    std::size_t max_resource_bytes;
    std::size_t max_framebuffer_pixels;
};
```

`src/render_core/budget.h` 会把这些值映射到当前 HTML/CSS parser、render/layout/layer/display-list、
dirty rectangle 和 frame-loop 入口。JerryScript runtime 构建还会使用 timer、listener 和
detached DOM node 上限。ESP32-S3 初始建议：

- DOM nodes：512-1500
- DOM depth：32-64
- attributes per element：16-32
- CSS rules：256-1024
- declarations per rule：64-128
- render/layout boxes：通常跟 DOM node 预算相同
- layers：64-256
- display commands：1024-4096
- dirty rects：4-16
- timers：16-32
- animation callbacks per frame：0-4，按产品功耗策略决定
- active animations：0-16
- animation frame rate：0、15 或 30 Hz，按前台/后台/息屏策略决定
- event listeners：128-256
- single resource：64-256 KiB
- framebuffer pixels：物理屏幕面积；如果使用 tiled output，可以更小

## 诊断 API

core 暂不强制 logging，但板级 port 应提供：

- panic/assert 输出；
- frame timing counters；
- 最大 heap watermark；
- dirty rectangle 数量/面积；
- resource load failures；
- 启用 JerryScript 时的 script exception reporting。

诊断应可在 release build 中裁掉。

## ESP32-S3 最小 Bring-Up 集合

优先实现：

- 从 flash 或嵌入式数组加载资源；
- monotonic `now_ms`；
- RGB565 framebuffer target 和屏幕 `flush(Rect)`；
- touch pointer down/up/move；
- 硬件按钮映射到 `activate_focused`；
- 可选表冠/encoder 映射到 `focus_next/focus_previous`；
- 覆盖 ASCII、数字、符号和选定中文子集的 bitmap font 测量/绘制。

之后再加：

- script timer pumping；
- text input method；
- dirty rectangle 合并策略；
- sleep/wake 显示策略；
- build-time font subsetting pipeline。
