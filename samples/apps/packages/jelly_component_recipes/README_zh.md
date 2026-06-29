# Component Recipes

面向 app 作者的小屏组件 recipes 示例。

- 顶栏、卡片栈、紧凑按钮和固定底部导航。
- 一个明确的 `overflow: auto` 内容区域，导航放在滚动区域外。
- 不依赖宿主服务、图片、Canvas 或 JavaScript。
- manifest 对 `round-300`、`rect-320x240` 和 `rect-172x320` 都设置了 gate。

验收命令：

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\jelly_component_recipes --target round-300 --targets round-300,rect-320x240,rect-172x320 --build-dir build\Release
```
