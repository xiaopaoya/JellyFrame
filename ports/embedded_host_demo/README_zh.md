# Embedded Host Demo

这个目录保存一个平台无关的 bring-up 示例。它放在 `ports/` 下，是因为它验证的是
更接近板级接入的能力，而不是桌面检查工具：

- 静态内存 HTML/CSS 资源；
- 紧凑的嵌入式风格 `HostBudgets`；
- bitmap 字体测量与绘制；
- 不依赖 Win32 的输入与焦点激活；
- RGBA framebuffer 到 RGB565 的转换与提交。

可执行文件名仍然是 `jellyframe_embedded_host_demo`。

这不是一个真实开发板 port。它的作用是给后续板级屏幕、输入、时钟和文本后端接线提供
最小参考形态。
