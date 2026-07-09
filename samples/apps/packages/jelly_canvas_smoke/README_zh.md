# Jelly Canvas Smoke

> 最后更新：2026-07-10；适用版本：0.5.0-dev

Canvas 2D V0.4 小示例，用于趋势线和简单图表。它先把图形绘制到紧凑的 source canvas，
再以有界的 canvas-to-canvas `drawImage()` 缩放到可见图表。页面结构和文字仍由普通 DOM/CSS 完成。

该示例声明 `graphics.canvas2d`。产品 target profile 需要显式开启后，package check
才会把 Canvas 视为支持能力。
