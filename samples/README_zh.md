# Samples 示例

> 最后更新：2026-08-26；适用版本：0.6.0-dev；Render Core 基线：0.6.1

这里保存 JellyFrame app 和 app-package 生命周期验证样例；桌面 native 工具位于
`../tools/native`。

## 按用途选择

| 目标 | 目录 |
| --- | --- |
| 创建新 app 的起始模板 | `../tools/templates/apps/` |
| 可读的可安装 app 展示与验收包 | `apps/packages/` |
| 特权启动器/系统壳样例 | `apps/system/` |
| 只验证 Render Core 的 HTML/CSS 页面 | `../src/render_core/samples/pages/` |
| 复现窄范围契约或失败 | `../tests/fixtures/apps/` |

`apps/packages/` 同时保存刻意收束的面向作者展示包和针对性验收包，具体分类见该目录
README。这里的样例用于验证和展示，不等于完整浏览器兼容契约。使用某项语法前，应先查
`../docs/developer_capability_matrix_zh.md` 和可搜索的 HTML/CSS 全表。

面向作者的展示包固定为 `watch_weather`、`jelly_controls`、
`jelly_motion_lab` 和 `jelly_route_tabs`。特权 `apps/system/band_system_shell`
仍是 172x320 系统壳验收样例，不是 app 作者起点。历史散文件视觉 probe 已移除：当前应
使用带 manifest 的 package 和确定性 capture 脚本。
