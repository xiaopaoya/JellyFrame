# Watch Weather 示例

一个紧凑的手表天气 source package，用于验证 package 结构、本地资源、包内 BMP 图片、XHR
数据更新、system-state JS binding、事件委托、小屏 grid 布局，以及 Win32/pseudo-browser
预览路径的一致性。

该示例保持 local-first，但会通过 JellyFrame `XMLHttpRequest` V0 子集向宿主数据服务请求
`/data/weather.json`。Win32 debug 壳提供 mock response；硬件 port 应提供自己的有界数据服务。
renderer 仍不加载远程页面。

Win32 scripting 壳可用 frame script 验证按钮事件、XHR mock、图片 completion 和帧更新统计：

```powershell
.\build-script\Release\jellyframe_win32_browser.exe --app samples\apps\packages\watch_weather --frame-script samples\apps\packages\watch_weather\capture_weather_interaction.jfcapture
```
