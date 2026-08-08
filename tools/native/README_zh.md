# Native Tools

> 最后更新：2026-08-09；适用版本：0.6.0-dev；兼容基线：0.5.0

这里保存用于检查 JellyFrame 输出的 C++ 桌面工具。示例页面和 app package 位于
`../../samples`。

- `*_dump.cpp` 工具用于输出 parser、DOM、CSSOM、render tree、layer tree 或完整管线结果。
- `pseudo_browser.cpp` 运行平台无关渲染管线，并可通过 `--diagnostics-json` 输出结构化 diagnostics，
  包括横向溢出、需要滚动、display command 密度过高等开发期视觉提示。
- `win32_browser.cpp` 是 Windows 验收壳，支持系统输入和截图。它可以打开散文件 HTML/CSS，
  也可以通过 `--app` 打开 source package。
- 动画验收推荐使用隐藏帧脚本：
  `--frame-script PATH`。脚本可以统一指定 deterministic time、逐帧 BMP 输出目录、拼图输出和事件注入。
  底层的 `--capture-frames DIR --frame-count 30 --frame-step-ms 33` 与多个
  `--frame-event FRAME:kind[:x:y[:delta]]` 仍可用于快速 smoke test。完整参数以
  `jellyframe_win32_browser --help` 输出为准。
  帧脚本中的 `pointer-down` 到 `pointer-up` 会保持 primary button 状态；期间的
  `pointer-move` 可用于确定性滑块拖动。`click` 仍表示一次完整按下/抬起。
- `win32_browser.cpp` 还提供 Win32-only host audio smoke 路径：
  `--audio-smoke local.wav`，或 `--app package --audio-smoke /audio/tone.wav`。
  这只验证 package resource 到桌面宿主 adapter 的交接，不代表嵌入式端内置 audio codec，
  也不代表已经暴露公开 JavaScript 音频 API。
- 隐藏逐帧 capture 会输出 host completion、system event、frame policy、service activity、
  per-app budget snapshot 和 scroll blit 统计。可以用这些计数验证 `backgroundServices`、
  息屏和低功耗策略是否按预期暂停或保留 network/audio/sensor/location 工作，而不需要让
  render core 了解硬件。
- 隐藏逐帧 capture 还会输出 `present_estimate_rgb565`。这是面向 16-bit RGB565 屏的
  嵌入式提交估算，不是 Win32 GDI 的真实工作量。它按桌面壳实际执行的 full-frame、
  dirty-rect 和 scroll-strip present 口径，估算 flush 次数、转换像素数和紧凑打包字节数。
  修改 dirty-region 或滚动复用逻辑前，可以用它和开发板端 `embedded_framebuffer`/panel
  日志对齐。
- debug image decode 与 debug network fetch 通过 `pump_app_host_service_workers(...)`
  泵送，与 MCU port 推荐的 request/completion 边界保持一致。

帧脚本是按行解析的极小格式：

```text
output-dir out/motion_lab_frames
montage out/motion_lab_montage.bmp
frames 30
step-ms 33
viewport 300 300
event 8 click 150 260
event 10 wheel 150 160 -120
event 11 escape
event 12 time-ms 1700000000123
event 14 battery 88 1
event 16 weather 213 rain
event 18 activity 6400 32
animation-fps 30
animation-callbacks 4
script-watchdog-checks 2048
script-watchdog-interval 16
```

JavaScript/rAF 播放需要使用 `JELLYFRAME_BUILD_SCRIPTING=ON` 配置出来的构建；
非 scripting 构建仍可验证 CSS animation。

在帧脚本中使用 `animation-fps 0` 和 `animation-callbacks 0`，或命令行传入
`--animation-fps 0 --animation-callbacks 0`，可以验证低功耗 profile：宿主应停止非必要动效，
但不需要修改 app 源码。

## 列表拖动验收

下面的确定性列表 fixture 用于检查常见宿主式纵向拖动。这是 scroll gesture，不是 HTML Drag and
Drop，也不是完整 Pointer Events 一致性验收。

```powershell
.\build\Release\jellyframe_win32_browser.exe --app tests\fixtures\apps\jelly_scroll_container_probe --frame-script tests\fixtures\apps\jelly_scroll_container_probe\capture_touch_drag_no_inertia.jfcapture
.\build\Release\jellyframe_win32_browser.exe --app tests\fixtures\apps\jelly_scroll_container_probe --frame-script tests\fixtures\apps\jelly_scroll_container_probe\capture_touch_drag_inertia.jfcapture
.\build\Release\jellyframe_win32_browser.exe --app tests\fixtures\apps\jelly_scroll_container_probe --frame-script tests\fixtures\apps\jelly_scroll_container_probe\capture_touch_drag_edge_stop.jfcapture
```

脚本会在 `out/` 写入逐帧 BMP 和拼图。慢拖必须报告 `inertia=0`；快速甩动必须报告正的 inertia
计数；边界甩动必须在容器到达边界后停止。既有的
`capture_touch_scroll_container.jfcapture` 仍是紧凑的 drag-plus-inertia 回归 fixture。

`event FRAME time-ms VALUE` 可为依赖 `Date.now()` 的表盘、计时器和天气样例注入确定性宿主时间。
这是壳层调试命令，不是 app 可见语法。

`event FRAME battery PERCENT CHARGING`、`event FRAME weather TEMP_C_X10 CONDITION`、
`event FRAME activity STEPS ACTIVE_MINUTES`、`event FRAME location LATITUDE LONGITUDE ACCURACY_M`
和 `event FRAME sensor KIND VALUE [Y Z]` 可在 Win32 capture 中验证 host-data snapshot 路径。
它们会更新壳层过滤后的 debug summary。只有 manifest 声明对应 `system.*` capability 时，
`battery`、`weather` 和 `activity` 才会通过 `navigator.jellyframe.getSnapshot()` 对 app 可见；
location 和 sensor 注入不会创建 JavaScript sensor API。

`script-watchdog-checks N`、`script-watchdog-interval N` 和
`require-script-watchdog` 只用于 Win32/scripted recovery 验收。它们映射到宿主的脚本执行预算，
并要求 JerryScript 构建启用 VM halt；app 作者不需要、也不应该依赖任何私有 JavaScript 语法。

这些 native 工具可以使用桌面文件 I/O。桌面 CMake 构建通过
`JELLYFRAME_ENABLE_IMAGE_FILE_IO=ON` 暴露 framebuffer 图片写出函数；嵌入式或
RTOS port 可以不定义该开关，使 render_core 不暴露文件写出入口。

`jellyframe_font_pack_gen` 用 BDF 输入生成离线 bitmap font pack。它既能输出固件用
C++ `BitmapFont` header，也能输出运行时 `.jffont` supplement。`--coverage-bits 1`
是紧凑单色路径；`--coverage-bits 2|4` 会生成显式 opt-in 的 glyph coverage 抗锯齿字体。
Coverage 字体会增加 glyph row 存储，并在绘制这些字体时多做 alpha blend；未使用这类字体的 app
不支付该成本。
