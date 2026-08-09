# Samples 示例

> 最后更新：2026-08-09；适用版本：0.6.0-dev；兼容基线：0.5.0

这里保存 JellyFrame app 和 app-package 生命周期验证样例；桌面 native 工具位于
`../tools/native`。

## 按用途选择

| 目标 | 目录 |
| --- | --- |
| 创建新 app 的起始模板 | `../tools/templates/apps/` |
| 可读的可安装 app 与 UI 示例 | `apps/packages/` |
| 特权启动器/系统壳样例 | `apps/system/` |
| 小型散文件视觉或脚本 probe | `apps/loose/` |
| 只验证 Render Core 的 HTML/CSS 页面 | `../src/render_core/samples/pages/` |
| 复现窄范围契约或失败 | `../tests/fixtures/apps/` |

这里的样例用于验证和展示，不等于完整浏览器兼容契约。使用某项语法前，应先查
`../docs/developer_capability_matrix_zh.md` 和可搜索的 HTML/CSS 全表。
