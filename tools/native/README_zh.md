# Native Tools

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
- `win32_browser.cpp` 还提供 Win32-only host audio smoke 路径：
  `--audio-smoke local.wav`，或 `--app package --audio-smoke /audio/tone.wav`。
  这只验证 package resource 到桌面宿主 adapter 的交接，不代表嵌入式端内置 audio codec，
  也不代表已经暴露公开 JavaScript 音频 API。
- 隐藏逐帧 capture 会输出 host completion、system event、frame policy、service activity、
  per-app budget snapshot 和 scroll blit 统计。可以用这些计数验证 `backgroundServices`、
  息屏和低功耗策略是否按预期暂停或保留 network/audio/sensor/location 工作，而不需要让
  render core 了解硬件。
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

`script-watchdog-checks N`、`script-watchdog-interval N` 和
`require-script-watchdog` 只用于 Win32/scripted recovery 验收。它们映射到宿主的脚本执行预算，
并要求 JerryScript 构建启用 VM halt；app 作者不需要、也不应该依赖任何私有 JavaScript 语法。

这些 native 工具可以使用桌面文件 I/O；嵌入式核心不依赖这些入口。

`jellyframe_font_pack_gen` 用 BDF 输入生成离线 bitmap font pack。它既能输出固件用
C++ `BitmapFont` header，也能输出运行时 `.jffont` supplement。`--coverage-bits 1`
是紧凑单色路径；`--coverage-bits 2|4` 会生成显式 opt-in 的 glyph coverage 抗锯齿字体。
Coverage 字体会增加 glyph row 存储，并在绘制这些字体时多做 alpha blend；未使用这类字体的 app
不支付该成本。
