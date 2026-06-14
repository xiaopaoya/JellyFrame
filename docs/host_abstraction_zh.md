# 宿主抽象草案

日期：2026-06-15

WearWeb 核心应继续独立于文件系统、网络栈、窗口系统、显示控制器、timer、输入硬件和字体 API。
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

这些接口还没有贯穿整个引擎。它们用于记录后续 shell/backend 应收敛到的稳定形状，
现有示例仍保留当前的 callback helpers。

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

当前 renderer 生成 `FrameBuffer`，桌面工具写 BMP/PPM 或通过 Win32/GDI blit。

`HostFrameSink` 应成为嵌入式显示提交边界：

- `HostFrameBufferView` 指向 RGBA pixels。
- `dirty_rects` 可选；为空表示全帧提交。
- 宿主负责把像素转成显示格式、DMA 或硬件 layer。

下一步应从 layer invalidation 生成 dirty rectangle。在那之前，software compositor 仍是全帧重绘。

## 文本后端

当前形状：

- `software_renderer.h` 中的 `TextPainter`
- Win32 壳注入 GDI 文本，用于可读 UTF-8/中文验证。
- 核心 fallback 保持极小并偏 ASCII。

未来嵌入式后端可以提供：

- bitmap font atlas；
- LVGL text draw bridge；
- vendor font engine；
- 面向生产级非拉丁文本的 shaping-capable text painter。

暂时不要把字体加载放进核心。

## 输入后端

当前形状：

- 宿主把 native mouse/wheel/key 转成 `PointerInput`、`WheelInput` 和 `KeyInput`。
- `InputController` 负责 hit testing、focus、activation 和 DOM event dispatch。

未来可穿戴输入 adapter 应映射：

- touch 到 pointer down/move/up；
- 表冠旋转到 wheel 或 app-specific events；
- 硬件按键到 focus navigation 和 activation；
- 长按到宿主定义命令。

现在缺的下一块是面向纯按键/表冠设备的小型 focus/navigation model。

## Budgets

宿主未来应配置：

- DOM node 上限；
- CSS rule/declaration 上限；
- display command 上限；
- timer 和 event listener 上限；
- resource byte 上限；
- framebuffer/offscreen buffer 策略。

当前 parser 已有一些固定限制。`HostBudgets` 记录的是后续集中化方向。

## 推荐顺序

1. 完成 M7 script loading 的示例和文档。
2. 添加 dirty rectangle invalidation，并接入 `HostFrameSink`。
3. 添加可部署的 embedded framebuffer backend。
4. 添加 Win32 之外的平台文本后端示例。
5. 把 resource/budget plumbing 贯穿 parser 和 scripting。
