# 嵌入式 HAL API

日期：2026-06-15

这份文档是 ESP32-S3、RTOS host 或具体开发板需要实现的接口清单。`jellyframe_core`
保持平台无关；所有真实 I/O 都由硬件程序负责，再通过小型 C++ struct 和 callback 调用核心。

## 必需运行循环

最小嵌入式 host 应执行以下循环：

1. 从 flash、partition、ROM bundle 或宿主存储加载 HTML/CSS/script 字节。
2. 构建 DOM、style、render、layout 和 layer tree。
3. 渲染到持久 `FrameBuffer`。
4. 通过 `HostFrameSink` 或 `embedded_framebuffer` 提交 dirty rectangles。
5. 把硬件输入转换成 `InputController` 调用。
6. 如果启用 scripting，泵动 JerryScript timers。
7. 如果 DOM dirty flags 发生变化，重建简化管线并重绘。

## 设备能力 API

头文件：`src/core/host.h`

```cpp
struct HostDeviceCapabilities {
    HostDisplayCapabilities display;
    HostInputCapabilities input;
    HostMemoryCapabilities memory;
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
- `budgets`：DOM/CSS/display-list/timer/listener/resource 限制；
- monotonic time、filesystem、network 等服务 flags。

ESP32-S3 手表起步建议：

- `preferred_pixel_format = HostPixelFormat::Rgb565`；
- `supports_partial_present = true`；
- 只有当 RAM/PSRAM 能同时容纳目标 buffer 和核心工作内存时，才设 `has_full_framebuffer = true`；
- 按开发板实际硬件设置 `touch`、`crown`/`focus_buttons`，或二者都启用；
- 除非产品宿主明确向 app 暴露有界网络/数据层，否则保持 `has_network = false`。

## 资源 API

头文件：`src/core/host.h`

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

头文件：`src/core/host.h`

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

- `src/core/software_renderer.h`
- `src/core/host.h`
- `src/core/embedded_framebuffer.h`

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

## 输入 API

头文件：`src/core/input.h`

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

- `src/core/text_backend.h`
- `src/core/software_renderer.h`

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
- 中文产品对子集化常用 app 字符，并用 `jellyframe_capability_check --font-coverage` 检查覆盖。

矢量字体在高配目标上可行，但 ESP32-S3 默认路线应是离线 rasterize 后的 bitmap glyph。

core 现在提供 `src/core/bitmap_font.h` 支撑这条默认路线。开发板 port 可以把生成的 glyph
数组暴露为 `BitmapFont`，再把 `bitmap_font_measure_callback` 接入 `LayoutEngine`，
把 `bitmap_font_paint_callback` 接入 `SoftwareCompositor`。

桌面生成器接受 BDF 输入：

```text
jellyframe_font_pack_gen --bdf font.bdf --chars used_chars.txt --output font_pack.h --name app_font
```

先用 `jellyframe_capability_check --emit-used-chars` 收集字符，再用你偏好的离线工具链从授权源字体生成 BDF。

## 平台无关 Bring-Up 示例

`jellyframe_embedded_host_demo` 是当前核心侧的开发板 bring-up 参考。它刻意避开 Win32、
文件、网络和硬件 I/O：

- 静态 HTML 和 CSS 直接编译进 executable；
- `BitmapFont` 提供测量和绘制 callbacks；
- `InputController::focus_next()` 和 `activate_focused()` 验证按键/表冠式交互；
- `embedded_frame_sink()` 把软件 RGBA frame 转换成 RGB565；
- 极小的 `flush(Rect)` callback 记录真实屏幕驱动应收到的 dirty area。

桌面运行：

```text
jellyframe_embedded_host_demo
```

输出应包含一次 flush、一次 button click、已勾选 checkbox、已变化 select value，以及非零前景像素。
开发板 port 应保持同样结构，只替换静态资源、字体包、输入来源和 framebuffer flush callback。

## Budgets

头文件：`src/core/host.h`

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
    std::size_t max_input_events_per_frame;
    std::size_t max_timer_callbacks_per_frame;
    std::size_t max_event_listeners;
    std::size_t max_resource_bytes;
    std::size_t max_framebuffer_pixels;
};
```

`src/core/budget.h` 会把这些值映射到当前 HTML/CSS parser、render/layout/layer/display-list、
dirty rectangle、frame-loop 和 JerryScript timer/listener 入口。ESP32-S3 初始建议：

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
