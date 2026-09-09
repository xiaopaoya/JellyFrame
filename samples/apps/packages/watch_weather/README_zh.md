# watch_weather

> 最后更新：2026-09-09；适用版本：0.6.0-dev；Render Core 基线：0.6.2

响应式数据卡、BMP、XHR 数据路由及 KV 降级。原有 Live/Offline 并列易误读，AQI 仍显示温度单位；窄屏按钮组高度预算错误。改为统一深底信息卡、明确 Sample/Demo、AQI 单位随模式切换，恢复选择时同步高亮。天气图标由本地几何生成，4 张 BMP 从 83,160 字节减至 49,368 字节。

```powershell
.\build\desktop-scripting-release\Release\jellyframe_desktop_shell.exe --app samples/apps/packages/watch_weather --frame-script samples/apps/packages/watch_weather/capture_review.jfcapture
```
