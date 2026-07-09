# 应用示例

> 最后更新：2026-07-10；适用版本：0.5.0-dev

这里保存完整 JellyFrame source-package 示例。每个 app 都应包含
`jellyframe.app.json`、本地 HTML/CSS/classic JavaScript，以及预览或打包所需的有界本地资源。

这些示例用于验证 runtime 行为和视觉验收。供开发者 CLI 复制的起始模板位于
`../../../tools/templates/apps`。

部分 package 声明了多个 target profile。可以用 CLI 的 responsive pass 检查同一个包在常见
可穿戴屏幕形态上是否仍然可用：

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\watch_weather --targets round-300,rect-320x240,rect-172x320 --build-dir build\Release
```

`watch_weather`、`jelly_controls`、`jelly_component_recipes` 和
`jelly_watch_face` 是主要展示/recipes package，应保持 target gate 与当前试用目标对齐。
音频、字体策略、动效压力和聚焦 Canvas 的 smoke package 可以故意不声明 hard gate，或保留
可解释 warning；它们用于验收某个子系统，不代表 polished release app。默认 `doctor` 会继续
扫描这些包，保证子系统 smoke 没有 error；判断外部试用展示质量时，应优先看声明了 target gate
的展示/recipes package。

当前 package：

- `watch_weather`：包含 package 资源和可选数据能力的手表天气 app。
- `jelly_controls`：Jelly UI 控件和动效风格示例。
- `jelly_component_recipes`：可复制的小屏按钮、卡片、滚动列表和固定底部导航 recipes。
- `jelly_motion_lab`：参考 LVGL 常见动效的验收 app，包含图标展开窗口、sheet 弹出和按钮果冻反馈。
- `jelly_watch_face`：使用 `transform: rotate(...)` 和 `transform-origin` 绘制指针的模拟表盘示例。
- `jelly_canvas_smoke`：可选 Canvas 2D V0.4 趋势线和柱状图示例，使用有界 canvas-to-canvas drawImage 缩放、径向高光与有预算的二次/三次 path。
- `jelly_canvas_gauges`：可选 Canvas 2D 仪表/圆环示例，覆盖 `arc`、`fill`、
  `globalAlpha`、Canvas 文本、线性渐变和有界 path 绘制。
- `jelly_service_status`：包含系统事件和本地存储的网络、音频、定位 service 边界示例。
- `jelly_audio_smoke`：用于 Win32 host-owned audio smoke 路径的包内音频资源示例。
- `jelly_font_policy`：用于说明 CSS `font-family` 与 `.jffont` 补充包策略的示例，
  覆盖两个 runtime family、缺字诊断和 Win32 `--use-app-fonts` 验收。
- `jelly_static_modules`：展示打包期静态本地 ES-module 图如何在 preview 或 packaging 前变成一个 classic device script。
- `jelly_route_tabs`：展示一个运行中 app 内有界的 `location.hash` tab 路由，不引入浏览器导航或 history。
