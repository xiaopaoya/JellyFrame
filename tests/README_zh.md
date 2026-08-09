# 测试目录

> 最后更新：2026-08-09；适用版本：0.6.0-dev；兼容基线：0.5.0

这里保存跨子项目 fixture 和桌面工具回归测试。修改代码时先进入对应模块的测试目录；
复现 app 或交互问题时先查 `fixtures/apps/` 的用途地图。C++ 单元测试仍与所属模块放在
一起，保持责任和构建依赖清晰。

| 目标 | 目录 | 证明内容 |
| --- | --- | --- |
| parser、style、layout、paint、input | `../src/render_core/tests` | 平台无关 Render Core 契约 |
| 生命周期、service、storage、package 状态 | `../src/app_runtime/tests` | 平台无关 runtime 契约 |
| JerryScript 和 script-task 协议 | `../src/script/tests` | 可选脚本行为和值协议边界 |
| CLI、packer、schema、profile、Win32 壳 | `tool_regression/` | 桌面开发工具契约 |
| 跨模块 UI 复现 | `fixtures/apps/` | 小型确定性输入/输出案例 |

fixture 不是展示样例：

- `jelly_flex_grid_probe`：Flex/Grid 几何。
- `jelly_scroll_probe`、`jelly_scroll_blit_probe`、`jelly_scroll_container_probe`：滚轮、拖动、惯性、边界和 strip blit。
- `jelly_dialog_modal`：dialog 关闭行为；`jelly_opacity_layer_reuse`：layer 重用。
- `jelly_budget_spam`、`jelly_service_spam`：预算和 service 压力。
- `jelly_watchdog_smoke`：故意死循环，用于 watchdog 恢复。
- `jelly_svg_icon`：打包期 SVG 栅格化边界。

要找可读的页面示例，请看 `samples/apps/packages/`；要创建新 app，请看 `tools/templates/`。
生成报告和截图只放在 `build*/test_outputs` 或 `out/`，不要提交。
