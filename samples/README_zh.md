# Samples

根 `samples` 只保存 JellyFrame app 和 app package 生命周期验收样例。原生桌面工具源码位于
`../tools/native`。

- `apps/packages/`：带 `jellyframe.app.json` 的完整 JellyFrame source package。
- `apps/loose/`：用于聚焦检查 runtime、scripting 和 rendering 的小型散文件 app fixture。

只属于 render core 的页面样例位于 `../src/render_core/samples`。
JerryScript 探针位于 `../src/script/samples`。
app 作者起始模板位于 `../tools/templates/apps`；samples 用于验证、截图和回归。
