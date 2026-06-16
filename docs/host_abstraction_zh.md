# 宿主抽象草案

日期：2026-06-15

JellyFrame 核心应继续独立于文件系统、网络栈、窗口系统、显示控制器、timer、输入硬件和字体 API。
宿主抽象层是桌面壳、RTOS task、LVGL driver 或自定义可穿戴平台向核心提供这些服务的边界。

这仍是很薄的草案，不是完整 HAL。它先固定真实嵌入式后端出现前最容易影响接口形状的部分。

## 设计规则

- 核心负责 parsing、DOM、style、layout、layer 组织、display-list generation、
  software rasterization 和平台无关事件。
- 宿主负责资源字节、墙钟时间、framebuffer 提交、硬件输入、字体/文本后端和设备能力。
- 核心 API 优先使用 callback 和 plain struct，不使用复杂继承层次。
- 宿主 callback 必须可选；缺失时应干净降级。
- 资源加载仍由本地/宿主提供，不暗含网络。
- 面向极小 MCU 前必须显式配置 budgets。

## 最小接口

`src/core/host.h` 现在定义了第一组小接口：

- `HostResourceRequest` 和 `HostResourceLoadCallback`
- `HostClock`
- `HostFrameBufferView`
- `HostFrameSink`
- `HostBudgets`
- `HostDeviceCapabilities`

这些接口现在已经轻量接入 presentation 路径：software renderer 可以把 `FrameBuffer`
暴露为 `HostFrameBufferView`，`present_frame` 可以调用 `HostFrameSink`，伪浏览器也通过
这条路径写出验收图片。资源加载、时钟、budgets 和设备能力仍处于草案形状。

## 设备能力

`HostDeviceCapabilities` 是开发板 port 提供的 plain description。核心不会通过它持有硬件状态；
它只记录后续策略可以读取的事实：

- 屏幕尺寸、DPI、首选像素格式、是否支持局部提交、是否能保留完整 framebuffer；
- touch、pointer、wheel、crown、focus buttons、keyboard 和 text input 等输入来源；
- heap 与最大单次分配估计；
- 显式 `HostBudgets`；
- 是否存在 monotonic time、filesystem 和 network 服务。

典型 ESP32-S3 手表目标可以从 RGB565、启用 partial present、按 RAM/PSRAM 布局设置
`has_full_framebuffer` 开始；touch/crown flags 按开发板实际硬件填写，并给出保守 heap/allocation
数字。filesystem 和 network 应视为可选宿主服务，而不是核心假设。

## 资源加载

当前已有具体 callback：

- CSS：`document_style.h` 中的 `StylesheetLoadCallback`
- Script：`document_script.h` 中的 `ScriptLoadCallback`

未来收敛方向：

```text
HostResourceLoadCallback(kind=Stylesheet or ClassicScript)
  -> bounded byte buffer
  -> parser or JerryScript runtime
```

规则：

- 相对路径由宿主解析，不由核心解析。
- 缺失资源保守忽略。
- 网络、ES modules 和 dynamic import 继续排除。
- 资源字节上限来自 `HostBudgets::max_resource_bytes`。

## 时间与 Timer

当前 scripting timer 由宿主泵动。宿主决定：

- 当前毫秒时间；
- 多久泵一次 timer；
- 每 tick 最大 callback 数；
- 低功耗模式是否延迟 callback。

`HostClock` 是未来替代 `GetTickCount64()` 等桌面调用的最小形状。

## Framebuffer 提交

当前 renderer 生成 `FrameBuffer`，桌面工具通过 frame sink 写 BMP/PPM，或通过 Win32/GDI blit。

`HostFrameSink` 应成为嵌入式显示提交边界：

- `HostFrameBufferView` 指向 RGBA pixels。
- `dirty_rects` 可选；为空表示全帧提交。
- 宿主负责把像素转成显示格式、DMA 或硬件 layer。

`SoftwareCompositor::render_into` 可以把调用方提供的 dirty rectangles 重绘进已有 framebuffer。
这是第一条面向嵌入式的 presentation 路径：宿主可以保留持久 framebuffer，请核心重绘有界区域，
再把相同 rectangles 提交给显示驱动。

`dirty_region` 现在提供第一版自动 rectangle 来源。它会对带有直接 dirty flags 的节点比较旧/新
layout box，为文本、属性和表单控件变化生成有界 rectangles。树结构变化仍保守请求全 viewport
重绘，因为被删除节点不能再通过旧 layout 指针安全寻址。这已经足够让 Win32 验证壳中的文本输入、
range/select 状态变化和小型脚本更新避免整帧清空。

`embedded_framebuffer` 是第一版可部署的宿主侧 adapter。它消费 `HostFrameBufferView`，
把 dirty rectangles 转换到调用方持有的 target buffer，并对每个 rectangle 调用可选 flush
callback。支持的 target 格式包括 RGBA8888、BGRA8888、RGB565、BGR565、RGB332、Gray8
和 1-bit 单色打包。

仍然缺少：

- 更细粒度的 layer/display-command invalidation；
- 可调 dirty rectangle 合并策略；
- 无完整 framebuffer 设备所需的 tiled/scanline presentation。

## 文本后端

当前形状：

- `text_backend.h` 中的 `TextMeasureProvider`
- `software_renderer.h` 中的 `TextPainter`
- Win32 壳注入 GDI 测量和绘制，用于可读 UTF-8/中文验证。
- 核心 fallback 保持极小：测量按 UTF-8 码点估算，绘制使用 ASCII bitmap，并为非 ASCII 码点绘制占位 glyph。

未来嵌入式后端可以提供：

- bitmap font atlas；
- LVGL text draw bridge；
- vendor font engine；
- 面向生产级非拉丁文本的 shaping-capable text painter。

暂时不要把字体加载放进核心。测量和绘制应来自同一个宿主字体引擎，避免裁切和换行不一致。参见 `docs/text_backend_zh.md`。

## 输入后端

当前形状：

- 宿主把 native mouse/wheel/key 转成 `PointerInput`、`WheelInput` 和 `KeyInput`。
- `InputController` 负责 hit testing、focus、activation 和 DOM event dispatch。

未来可穿戴输入 adapter 可映射：

- touch 到 pointer down/move/up；
- 表冠旋转到 wheel 或 app-specific events；
- 硬件按键到 focus navigation 和 activation；
- 长按到宿主定义命令。

面向纯按键/表冠设备的第一版 focus/navigation API 已在 `InputController` 上提供：
`focus_next()`、`focus_previous()` 和 `activate_focused()`。

## Budgets

宿主应配置：

- DOM node 上限；
- CSS rule/declaration 上限；
- display command 上限；
- timer 和 event listener 上限；
- resource byte 上限；
- framebuffer/offscreen buffer 策略。

`HostBudgets` 已通过 `src/core/budget.h` 贯穿 HTML/CSS parser、render/layout/layer、
display-list、dirty rectangle 和 scripting 的主要入口。后续重点不是再定义预算结构，
而是补齐更细的 offscreen/tile buffer 策略，以及预算超限路径的长时间稳定性测试。

## 推荐顺序

1. 收敛运行循环和 dirty update 契约。
2. 做更细的 invalidation/subtree reuse。
3. 完善文本后端 adapter 和字体工作流验证。
4. 整理本地资源包与 app packaging。
