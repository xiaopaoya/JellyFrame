# 项目维护工具

> 最后更新：2026-08-28；适用版本：0.6.0-dev；兼容基线：0.5.0

这里放 CI、Render Core 裁剪、性能守卫和 HTML/CSS 兼容性表维护工具。
App 作者通常从 `tools/jellyframe_cli.py` 或 VS Code 扩展开始。

| 目标 | 入口 |
| --- | --- |
| 检查桌面性能回归 | `benchmark_guard.py` |
| 检查 Render Core profile 和 link map | `check_render_core_link_map.py` |
| 查看或维护 Render Core 能力目录 | `render_core_feature_registry.py` |
| 打包 App 作者 SDK | `package_app_author_sdk.py` |
| 生成 HTML/CSS 支持表 | `generate_html_support_table.py`、`generate_css_support_table.py` |
| 导入 CSS crosswork 快照 | `import_css_support_crosswork.py` |

这些工具不是运行时依赖。报告默认写入被忽略的 build 或审查产物目录，只有明确
提升为项目文档的报告才应进入版本控制。

`package_app_author_sdk.py` 是发布维护工具：它从已验证的桌面 Release（可选 scripting
Release）生成版本化 ZIP，只包含 App 作者需要的 CLI、模板、schema、target preset、feature
registry 与桌面运行时。它不得代替完整源码、port 或 Device OS 发布；具体作者环境约定见
[`../docs/app_author_environment_zh.md`](../docs/app_author_environment_zh.md)。
