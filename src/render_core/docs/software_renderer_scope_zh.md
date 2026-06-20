# Software Renderer 裁剪范围


JellyFrame 现在包含一个纯 CPU 的验收渲染器。它的目的不是替代最终硬件后端，
而是在不假设 GPU、显示控制器或嵌入式窗口系统的前提下，证明完整管线可以产出像素。

参考点：

- CSS 2 painting order 和 stacking 是 display-list 顺序的现实基础。
- CSS Compositing and Blending Level 1 定义了这里使用的 normal source-over
  compositing model。
- HTML 的 rendering 章节偏建议性；JellyFrame 为可穿戴目标保持小而确定的实现。

## 管线

```text
DOM + CSSOM
  -> computed style
  -> render tree
  -> layout tree
  -> layer tree
  -> display commands
  -> SoftwareRasterizer
  -> SoftwareCompositor
  -> FrameBuffer
  -> BMP / PPM output
```

## 已实现

- 使用 integer RGBA pixels 的 straight-alpha `FrameBuffer`。
- `SoftwareCompositor::render()` 支持可选主 framebuffer pixel budget；超限时会在分配像素前返回空 framebuffer。
- Source-over alpha compositing。
- Rectangle fills、stroke rectangles、便宜近似 `box-shadow` 填充，以及简单垂直
  linear-gradient command。
- 背景填充的 rounded rectangle clipping。
- Windows 下通过 GDI CPU mask 绘制文本。
- 非 Windows 构建保留内置 tiny ASCII fallback text drawing。
- 针对 opacity/composited layers 的离屏合成。
- 对 composited layer 执行 `transform: translate()/scale()` 子集：平移为整数像素近似，缩放以 layer bounds
  中心为原点并采用最近邻采样，适合按钮反馈和卡片滑动，不追求浏览器级像素一致。
- 可选 offscreen pixel budget：过大的 composited layer 会降级为逐命令透明绘制，
  避免分配大块临时 RGBA framebuffer。
- 用于 pseudo-browser 验收的 BMP 和 PPM 图片写出。

## 明确裁剪

- 不使用 GPU surfaces。
- 不支持 normal source-over 以外的 blend modes。
- 不支持 filters、backdrop filters 或真实 blur shadow。
- 不做完整 text shaping、bidi 或 font fallback stack。
- 不做 image decode。
- 不做 subpixel layout 或 antialiased geometry。
- 不做 rotate/skew/matrix/perspective，也不做完整 transform-origin。

## 当前兼容性说明

该 renderer 足以暴露灾难性管线问题：盒子缺失、CSS fallback 失效、裁剪错误、
空输出和文本编码问题。它还不是像素兼容的浏览器 renderer。

近期修复包括 Windows UTF-8 文本输出、保守文本 overhang padding、基础多行文本绘制、
`box-sizing:border-box`、常见 `rgb()/rgba()` 颜色、四值 box edges，以及最小 flex
居中/横向布局、响应式 grid card layout、`aspect-ratio` 尺寸计算和便宜圆角
`box-shadow` 近似，以及第一版 `opacity`/2D transform 合成动画基础。
