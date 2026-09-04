# App Packages

> 最后更新：2026-09-04；适用版本：0.6.0-dev；Render Core 基线：0.6.2

这里保存完整 JellyFrame source-package 示例。每个 app 都应包含
`jellyframe.app.json`、本地 HTML/CSS/classic JavaScript，以及预览或打包所需的有界本地资源。

展示包用于阅读 app 结构和当前视觉写法。供开发者 CLI 复制的起始模板位于
`../../../tools/templates/apps`。其余 package 是验收输入：它们刻意覆盖一个有界能力或
宿主边界，不建议作为 app 作者起点。

部分 package 声明了多个 target profile。可以用 CLI 的 responsive pass 检查同一个包在常见
可穿戴屏幕形态上是否仍然可用：

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\watch_weather --targets round-300,rect-320x240,rect-172x320 --build-dir build\desktop-release\Release
```

## 面向作者的展示包

- `watch_weather`：包内图片、宿主形态的数据更新和紧凑的响应式状态卡片。
- `jelly_controls`：原生表单控件、focus/pressed 状态和本地 UI 状态。
- `jelly_motion_lab`：当前 paint-safe CSS 与脚本化动画回放。
- `jelly_route_tabs`：不引入导航的有界 app-local route state。

四个展示包都必须保持可读、经过目检，并可在当前桌面壳中确定性运行。`doctor --trial`
仍可在需要特定契约时使用针对性验收包。

## 验收包

- `jelly_canvas_smoke`：可选 Canvas 2D V0.4 趋势线和柱状图示例，使用有界 canvas-to-canvas drawImage 缩放、径向高光与有预算的二次/三次 path。
- `jelly_canvas_gauges`：可选 Canvas 2D 仪表/圆环示例，覆盖 `arc`、`fill`、
  `globalAlpha`、Canvas 文本、线性渐变和有界 path 绘制。
- `jelly_service_status`：包含系统事件和本地存储的网络、音频、定位 service 边界示例。
- `jelly_audio_smoke`：用于 Win32 host-owned audio smoke 路径的包内音频资源示例。
- `jelly_font_policy`：用于说明 CSS `font-family` 与 `.jffont` 补充包策略的示例，
  覆盖两个 runtime family、缺字诊断和 Win32 `--use-app-fonts` 验收。
- `jelly_static_modules`：展示打包期静态本地 ES-module 图如何在 preview 或 packaging 前变成一个 classic device script。
- `jelly_component_recipes`：scroll/dirty-region 与组件结构回归输入；面向作者的 recipe 正文在 `docs/app_author_recipes_zh.md`。
- `jelly_watch_face`：transform/radius/gradient 时序回归输入。
- `jelly_wearable_launcher`：图标网格绘制回归输入。
