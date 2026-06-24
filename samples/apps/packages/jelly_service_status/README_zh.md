# Service Status

用于验证可选数据、媒体和定位服务边界的小型 runtime 示例。

它声明网络、存储、MP3 播放和定位能力，并通过 `backgroundServices` 表达 app 在
suspended 或 screen-off 状态下希望继续哪些后台活动。manifest 只表示意图；
真实设备上仍必须由宿主 profile 决定是否批准。

页面只使用本地 HTML/CSS/JS。它读取 `navigator.onLine` 和 `document.hidden`，
通过 `XMLHttpRequest` 请求 `/data/service-status.json`，请求一次
`navigator.geolocation.getCurrentPosition(...)` 定位快照，并把最近一次服务状态写入
`localStorage` shadow，方便 Win32 壳同时验证系统状态投递、host completion 和小型非阻塞存储。

确定性 Win32 验收命令：

```powershell
.\build-script\Release\jellyframe_win32_browser.exe `
  --app samples\apps\packages\jelly_service_status `
  --frame-script samples\apps\packages\jelly_service_status\capture_system_events.jfcapture
```

capture 汇总中应能看到非零 network/location host completion、脚本处理的 system event，以及
`service_activity` 计数。这个脚本里，screen hidden 期间 network 会按 manifest 策略停止，
audio 继续允许，sensors 会在息屏或低功耗帧中被节流。
