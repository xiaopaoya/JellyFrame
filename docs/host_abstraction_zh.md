# 宿主抽象草案


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

`src/render_core/host.h` 现在定义了第一组小接口：

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
- 异步 job、媒体 decode/playback、runtime network fetch 和 installable bundle store 的可选能力；
- 显式 `HostBudgets`；
- 是否存在 monotonic time、filesystem 和 network 服务。

典型 ESP32-S3 手表目标可以从 RGB565、启用 partial present、按 RAM/PSRAM 布局设置
`has_full_framebuffer` 开始；touch/crown flags 按开发板实际硬件填写，并给出保守 heap/allocation
数字。filesystem 和 network 应视为可选宿主服务，而不是核心假设。

## 慢任务与可选服务

`HostDeviceCapabilities` 现在把慢任务相关能力拆成四组：

- `async`：宿主是否能在 UI task 外执行 job，是否支持取消，以及每帧 completion event 上限；
- `media`：图片 decode、音频 playback、轻量视频 decode 能力和硬尺寸/缓冲上限；
- `network`：运行时数据 fetch 能力，请求/响应大小上限，远程页面资源开关；
- `app_bundles`：第三方 flash bundle 的安装、完整性校验和容量上限。

这些字段不是要求核心直接调用硬件。它们是 port、桌面调试器、打包器和未来 JS API
对齐策略的描述：某个 app 声明需要 `network.fetch` 或 `media.audio.mp3` 时，工具可以在打包/安装前
比对目标 profile，运行时也能在绑定 API 前决定是否暴露能力。

推荐的执行边界：

- UI/main task 独占 DOM、JerryScript、style/layout/layer、dirty region 和 framebuffer。
- Decode/network/install/file I/O 只能在 host worker、RTOS task 或系统服务中执行。
- Worker 完成后只把小型 completion event 投回 UI 队列。
- UI 帧循环按预算消费有限 completion events，再标记 DOM dirty 或派发 JS event。
- Worker 不得直接持有 DOM 节点裸指针，不得调用 layout/render，不得写 framebuffer。

这个模型让外部安装式 app、网络请求和音频播放可以共存，而不会让系统/App 主进程因为一个慢
decode 或 HTTP request 卡住。

ESP32-S3 解码实验包的结论应映射为 profile，而不是核心默认功能：MP3 音频播放和小尺寸
MJPEG/图片 decode 可以作为可选 host service；H.264 在 2026-06-20 QEMU + Octal PSRAM 复测中
已能跑通，但低分辨率 baseline 样本仍低于实时，应保持显式实验性或关闭。

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

## Vendor 与 LVGL 后端策略

JellyFrame 的主渲染路径应继续独立于 LVGL 和厂商 UI widget tree。核心负责 HTML/CSS parsing、DOM/style/layout/layer 构建、软件 framebuffer 渲染和输入事件派发。开发板 port 可以把 LVGL 或厂商 BSP 当作很薄的适配层，用于屏幕初始化、触摸/背光初始化、文本测量/绘制 callback，或最终 dirty rectangle flush；但不应把 JellyFrame DOM/CSS/layout 映射成 LVGL widget tree。那会让 layout、style、focus、event 和 font 形成两套系统，难以验收，也会抬高移植复杂度。

ESP32-S3 推荐路径是：JellyFrame software `FrameBuffer` -> `embedded_framebuffer` RGB565 转换 -> `flush(Rect)`/`packed_flush(Rect)` -> `esp_lcd_panel_draw_bitmap` 或等价屏幕驱动调用。输入从 board queue 进入 `InputController`；文本通过 `TextMeasureProvider` 和 `TextPainter` 注入。如果厂商 SDK 强绑定 LVGL，只包装最终 panel/input/text hooks，`src/render_core` 不应包含 LVGL 头文件。

## 文本后端

当前形状：

- `text_backend.h` 中的 `TextMeasureProvider`
- `software_renderer.h` 中的 `TextPainter`
- Win32 壳注入 GDI 测量和绘制，用于可读 UTF-8/中文验证。
- 核心 fallback 保持极小：测量按 UTF-8 码点估算，绘制使用 ASCII bitmap，并为非 ASCII 码点绘制占位 glyph。

嵌入式后端可以提供：

- 面向 `tiny`、app-specific、`cn-standard` 或按市场划分 profile 的生成式 bitmap font pack；
- 仅作为宿主文本 hook 的 LVGL text draw bridge；
- vendor font engine；
- 面向确实需要复杂文字系统的 shaping-capable text painter。

不要让 render core 自己读取或解析任意字体文件。当前生产路径是编译期 bitmap font pack；后续高优先级的
`.jfapp` 包内动态字体补充也应通过受控 font resource provider 和同一个宿主文本后端接入。测量和绘制
必须来自同一套 glyph metrics，避免裁切和换行不一致。参见 `src/render_core/docs/text_backend_zh.md`。

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
- timer 数量、event listener、detached DOM node 和每帧 input/timer callback 上限；
- resource byte 上限；
- framebuffer/offscreen buffer 策略。

`HostBudgets` 已通过 `src/render_core/budget.h` 贯穿 HTML/CSS parser、render/layout/layer、
display-list、dirty rectangle 和 frame-loop 工作上限。JerryScript runtime 构建也会使用
timer、listener 和 detached DOM node 上限。软件 compositor 也可以使用由 framebuffer 预算
派生出的主 framebuffer 与 offscreen pixel 上限。后续重点不是再定义预算结构，而是补齐 tile/scanline
presentation 策略，以及预算超限路径的长时间稳定性测试。

## 推荐顺序

1. 整理本地资源包与 app packaging。
2. 继续 allocator 工作，并用 dirty-region 诊断判断 retained subtree reuse 是否值得承担所有权复杂度。
3. 在真实硬件压力要求时，再细化 tile/scanline presentation 策略。
