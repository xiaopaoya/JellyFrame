# clock

> 最后更新：2026-09-09；适用版本：0.6.0-dev；Render Core 基线：0.6.2

Date.now 宿主时间、timer、时区偏移。原版每秒轮播四条预设时间与健康数据，Zone 只改文字。新版按 Date.now 算 UTC/UTC+8，支持 12/24 小时制，进度条表示一天的时间。没有虚构健康读数。

遵循[模板约定](../CONVENTIONS_zh.md)。

```powershell
.\build\desktop-scripting-release\Release\jellyframe_desktop_shell.exe --app tools/templates/apps/clock --frame-script tools/templates/apps/clock/capture_review.jfcapture
```
