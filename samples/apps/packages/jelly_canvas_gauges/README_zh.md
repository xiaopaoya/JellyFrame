# Jelly Canvas Gauges

> 最后更新：2026-08-26；适用版本：0.6.0-dev；Render Core 基线：0.6.1

面向可穿戴仪表盘和紧凑数据图形的 Canvas 2D V0.4 示例。布局、文本和控件仍由 DOM/CSS
负责；Canvas 只用于环形仪表、标签和很难用盒模型表达的小图表。

该 package 声明 `graphics.canvas2d`。目标平台需要通过 `hostServices.canvas2d`
显式开启支持。它的响应式 CSS 和 target gate 已覆盖 `round-300`、`rect-320x240` 与
`rect-172x320`；用 `jellyframe_cli.py doctor --sample jelly_canvas_gauges --strict`
可以复验这项契约。这是验收 package，不是 app 作者 starter 或门面示例。只有明确绑定 Canvas
的宿主才能把其视觉输出作为 Canvas 证据。
