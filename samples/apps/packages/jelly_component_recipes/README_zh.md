# Component Recipes

> 最后更新：2026-08-26；适用版本：0.6.0-dev；Render Core 基线：0.6.1

常见可穿戴 UI 结构的确定性 scroll/dirty-region 验收输入。可复制的作者 recipe 正文维护在
`docs/app_author_recipes_zh.md`；此 package 保持聚焦回放覆盖。

- 顶栏、圆屏优先状态卡片、卡片栈、紧凑按钮、控制行和固定底部导航。
- 一个明确的 `overflow: auto` 内容区域，导航放在滚动区域外。
- 不依赖宿主服务、图片、Canvas 或 JavaScript。
- manifest 对 `round-300`、`rect-320x240` 和 `rect-172x320` 都设置了 gate。

验收命令：

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\jelly_component_recipes --target round-300 --targets round-300,rect-320x240,rect-172x320 --build-dir build\desktop-release\Release
```

验证 present / dirty-rect 行为时，可运行确定性滚动 capture：

```powershell
.\build\desktop-release\Release\jellyframe_desktop_shell.exe --app samples\apps\packages\jelly_component_recipes --frame-script samples\apps\packages\jelly_component_recipes\capture_scroll_recipes.jfcapture
```

摘要中应看到内部滚动走 dirty repaint，且没有非首帧 full repaint。
