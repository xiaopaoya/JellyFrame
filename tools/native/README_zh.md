# Native Tools

这里保存用于检查 JellyFrame 输出的 C++ 桌面工具。示例页面和 app package 位于
`../../samples`。

- `*_dump.cpp` 工具用于输出 parser、DOM、CSSOM、render tree、layer tree 或完整管线结果。
- `pseudo_browser.cpp` 运行平台无关渲染管线，并可通过 `--diagnostics-json` 输出结构化 diagnostics。
- `win32_browser.cpp` 是 Windows 验收壳，支持系统输入和截图。它可以打开散文件 HTML/CSS，
  也可以通过 `--app` 打开 source package。
- 动画验收推荐使用隐藏帧脚本：
  `--frame-script PATH`。脚本可以统一指定 deterministic time、逐帧 BMP 输出目录、拼图输出和事件注入。
  底层的 `--capture-frames DIR --frame-count 30 --frame-step-ms 33` 与多个
  `--frame-event FRAME:kind[:x:y]` 仍可用于快速 smoke test。

帧脚本是按行解析的极小格式：

```text
output-dir out/motion_lab_frames
montage out/motion_lab_montage.bmp
frames 30
step-ms 33
viewport 300 300
event 8 click 150 260
```

JavaScript/rAF 播放需要使用 `JELLYFRAME_BUILD_SCRIPTING=ON` 配置出来的构建；
非 scripting 构建仍可验证 CSS animation。

这些 native 工具可以使用桌面文件 I/O；嵌入式核心不依赖这些入口。
