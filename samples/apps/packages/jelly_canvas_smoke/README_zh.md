# Jelly Canvas Smoke

> 最后更新：2026-08-26；适用版本：0.6.0-dev；Render Core 基线：0.6.1

Canvas 2D V0.4 小示例，用于趋势线和简单图表。它先把图形绘制到紧凑的 source canvas，用有界双 stop 同心 `createRadialGradient()` 作为背景，
再以有界的 canvas-to-canvas `drawImage()` 缩放到可见图表。页面结构和文字仍由普通 DOM/CSS 完成。

该示例声明 `graphics.canvas2d`。产品 target profile 需要显式开启后，package check
才会把 Canvas 视为支持能力。这是验收 package，不是 app 作者 starter 或门面示例。当前
Win32 gallery shell 不宣称提供产品级 Canvas host binding；只有宿主明确启用 Canvas 时，才用它
验证有界 API 与 target 声明。
