# App 测试 Fixtures

> 最后更新：2026-08-09；适用版本：0.6.0-dev；兼容基线：0.5.0

这里是小型、确定性的复现 package，不是经过美化的 app 示例。按要检查的契约选择：

| 范围 | Fixtures |
| --- | --- |
| 布局 | `jelly_flex_grid_probe` |
| 滚动和提交 | `jelly_scroll_probe`、`jelly_scroll_blit_probe`、`jelly_scroll_container_probe` |
| modal/input | `jelly_dialog_modal` |
| layer invalidation | `jelly_opacity_layer_reuse` |
| 预算/恢复 | `jelly_budget_spam`、`jelly_watchdog_smoke` |
| 宿主 service 压力 | `jelly_service_spam` |
| 打包期 SVG 边界 | `jelly_svg_icon` |

`.jfcapture` 可使用 `../../../tools/native/README_zh.md` 中的 Win32 壳运行，
也可以运行对应 CTest。新增 fixture 应保持最小且便于定位；展示样例放在
`samples/apps/packages/`，作者模板放在 `tools/templates/apps/`。
