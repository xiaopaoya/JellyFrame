# Samples

根 `samples` 只保存 JellyFrame app 和 app package 生命周期验收样例。原生桌面工具源码位于
`../tools/native`。

- `apps/packages/`：带 `jellyframe.app.json` 的完整 JellyFrame source package。
- `apps/system/`：特权系统 app 样例，例如 Win32 app-manager host path 使用的示例启动器。
- `apps/loose/`：用于聚焦检查 runtime、scripting 和 rendering 的小型散文件 app fixture。

当前视觉系统示例：

- `apps/packages/jelly_controls`：可安装 Jelly UI 控件 package。
- `apps/loose/jelly_motion.html`：transition/keyframe 动效 fixture。
- `apps/loose/jelly_launcher_mock.html`：启动器 grid 视觉 fixture。

只属于 render core 的页面样例位于 `../src/render_core/samples`。
JerryScript 探针位于 `../src/script/samples`。
app 作者起始模板位于 `../tools/templates/apps`；samples 用于验证、截图和回归。
