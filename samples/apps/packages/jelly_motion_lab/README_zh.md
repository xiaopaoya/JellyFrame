# Jelly Motion Lab

用于验证手表风格 JellyFrame 动效的可安装样例。它只使用标准 CSS
`@keyframes`、`transform`、`opacity`、transition 和 `requestAnimationFrame`，
不需要自定义动效 API。

Win32 逐帧验收示例：

```powershell
.\build\Release\jellyframe_win32_browser.exe --app samples\apps\packages\jelly_motion_lab --capture-frames out\motion_lab_frames --frame-count 30 --frame-step-ms 33 --viewport-width 300 --viewport-height 300
```

推荐使用可复现的帧脚本，一次输出 30 帧和拼图：

```powershell
.\build\Release\jellyframe_win32_browser.exe --app samples\apps\packages\jelly_motion_lab --frame-script samples\apps\packages\jelly_motion_lab\capture_30fps.jfcapture
```

更长的 30fps soak 回归：

```powershell
.\build\Release\jellyframe_win32_browser.exe --app samples\apps\packages\jelly_motion_lab --frame-script samples\apps\packages\jelly_motion_lab\capture_soak_30fps.jfcapture
```

低功耗预算冒烟测试：

```powershell
.\build\Release\jellyframe_win32_browser.exe --app samples\apps\packages\jelly_motion_lab --frame-script samples\apps\packages\jelly_motion_lab\capture_low_power_static.jfcapture
```

帧脚本也可以设置 `animation-fps` 和 `animation-callbacks`，用于在不修改 app 源码的情况下验证宿主预算行为。
