# timer

> 最后更新：2026-09-09；适用版本：0.6.0-dev；Render Core 基线：0.6.2

基于 deadline 的倒计时、按钮状态、两段 conic 环。原版累计 tick、进度环不变、1/4 会超过 4，功能更像调试秒表。新版为一分钟计时器，支持开始/暂停/复位/完成/再次开始，进度环与剩余时间一致。

遵循[模板约定](../CONVENTIONS_zh.md)。

```powershell
.\build\desktop-scripting-release\Release\jellyframe_desktop_shell.exe --app tools/templates/apps/timer --frame-script tools/templates/apps/timer/capture_review.jfcapture
```
