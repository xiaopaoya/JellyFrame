# calculator

> 最后更新：2026-09-09；适用版本：0.6.0-dev；Render Core 基线：0.6.2

四列 grid、dataset 委托、有界整数状态。原版无运算时按等号会回到旧 pending；链式运算覆盖前项，输入无界。新版明确只做整数加减，8 位输入限制、结果边界、重复等号稳定、顺序链式计算。

遵循[模板约定](../CONVENTIONS_zh.md)。

```powershell
.\build\desktop-scripting-release\Release\jellyframe_desktop_shell.exe --app tools/templates/apps/calculator --frame-script tools/templates/apps/calculator/capture_review.jfcapture
```
