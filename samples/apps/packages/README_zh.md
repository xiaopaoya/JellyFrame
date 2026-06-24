# 应用示例

这里保存完整 JellyFrame source-package 示例。每个 app 都应包含
`jellyframe.app.json`、本地 HTML/CSS/classic JavaScript，以及预览或打包所需的有界本地资源。

这些示例用于验证 runtime 行为和视觉验收。供开发者 CLI 复制的起始模板位于
`../../../tools/templates/apps`。

部分 package 声明了多个 target profile。可以用 CLI 的 responsive pass 检查同一个包在常见
可穿戴屏幕形态上是否仍然可用：

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\watch_weather --targets round-300,rect-320x240,rect-172x320 --build-dir build\Release
```

当前 package：

- `watch_weather`：包含 package 资源和可选数据能力的手表天气 app。
- `jelly_controls`：Jelly UI 控件和动效风格示例。
- `jelly_motion_lab`：参考 LVGL 常见动效的验收 app，包含图标展开窗口、sheet 弹出和按钮果冻反馈。
- `jelly_service_status`：包含系统事件和本地存储的网络、音频、定位 service 边界示例。
- `jelly_audio_smoke`：用于 Win32 host-owned audio smoke 路径的包内音频资源示例。
- `jelly_font_policy`：用于说明 CSS `font-family` 与 `.jffont` 补充包策略的示例。
