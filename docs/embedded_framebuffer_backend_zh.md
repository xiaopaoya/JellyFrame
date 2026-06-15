# 嵌入式 Framebuffer 后端

日期：2026-06-15

`src/core/embedded_framebuffer.h` 提供第一版可部署的嵌入式 presentation
adapter。它仍保持平台无关：不打开设备、不分配显示内存、不启动 DMA、不调用 LVGL，也不调用
RTOS API。它只把 JellyFrame 内部 RGBA `FrameBuffer` view 转换到调用方提供的显示 buffer，
并把相同 dirty rectangles 交回宿主。

## 支持格式

| 格式 | 存储 | 用途 |
| --- | --- | --- |
| `Rgba8888` | 每像素 4 字节，RGBA 顺序 | 验证或 32-bit 显示 buffer。 |
| `Bgra8888` | 每像素 4 字节，BGRA 顺序 | 使用 BGRA scanout 的宿主。 |
| `Rgb565` | little-endian 16-bit RGB 5:6:5 | 常见 MCU/TFT 屏格式。 |
| `Bgr565` | little-endian 16-bit BGR 5:6:5 | 红蓝通道接线/格式互换的屏。 |
| `Rgb332` | 8-bit RGB 3:3:2 | 极小彩色 buffer。 |
| `Gray8` | 8-bit 亮度 | 灰度屏或调试输出。 |
| `Mono1Msb` | 每像素 1 bit，最左像素在 bit 7 | 常见单色屏控制器打包方式。 |
| `Mono1Lsb` | 每像素 1 bit，最左像素在 bit 0 | 另一类单色屏打包方式。 |

转换前会压平 alpha。正常渲染下 source framebuffer 已经合成到不透明背景上，所以这主要是兜底路径。

## 宿主契约

宿主负责：

- target byte buffer；
- 显示控制器或图形库；
- 双缓冲、DMA 生命周期等策略；
- 可选 flush callback，用于提交每个 dirty rectangle。

核心只负责转换：

```cpp
std::vector<std::uint8_t> display(
    embedded_framebuffer_min_size(width, height, EmbeddedPixelFormat::Rgb565));

EmbeddedFrameBufferSink embedded{
    EmbeddedFrameBufferTarget{width,
                              height,
                              EmbeddedPixelFormat::Rgb565,
                              display.data(),
                              display.size(),
                              0},
    flush_rect_to_panel,
    panel_context};

HostFrameSink sink = embedded_frame_sink(embedded);
present_frame(frame_buffer, sink, dirty_rects, dirty_rect_count);
```

如果 `dirty_rects` 为空，则转换整帧。否则只转换并 flush 裁剪后的 dirty rectangles。
这些 rectangles 外的像素保持不变。

## 限制

- source 和 target 尺寸必须完全一致。
- adapter 不分配也不持有内存。
- adapter 不合并 dirty rectangles；上游 `dirty_region` 决定 rectangle list。
- adapter 不执行硬件滚动 layer，也不设置局刷 address window；这些由宿主在 `flush` 中完成。
- 文本质量仍取决于 renderer 的 `TextPainter`，不由该 adapter 决定。

## 为什么这样设计

小设备差异极大：有的暴露线性 framebuffer，有的需要 SPI address-window 写入，有的接 LVGL draw
buffer，有的用 RGB565，有的是单色屏。plain byte-buffer converter 加 rectangle callback
能让核心保持可复用，同时明确最重要的嵌入式规则：不要重绘或提交 dirty region 之外的像素。
