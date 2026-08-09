# Route Tabs

> 最后更新：2026-07-12；适用版本：0.5.0

此示例使用受限的 app 内 `location.hash` 路由来实现设置/专注页签流。它只改变当前运行 app 的路由状态，不加载 URL，也没有浏览器历史或导航栈。

```powershell
python tools\jellyframe_cli.py preview --root samples\apps\packages\jelly_route_tabs --output build\route_tabs.bmp --build-dir build\Release
```

可用以下命令进行确定性的 Win32 交互 capture：

```powershell
.\build\Release\jellyframe_desktop_shell.exe --app samples\apps\packages\jelly_route_tabs --frame-script samples\apps\packages\jelly_route_tabs\capture_route_tabs.jfcapture
```
