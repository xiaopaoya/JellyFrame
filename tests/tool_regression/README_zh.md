# 工具回归测试

> 最后更新：2026-08-09；适用版本：0.6.0-dev；兼容基线：0.5.0

这里保存开发工具层的轻量回归测试，主要锁住 package/check 这类不属于
render-core 或 app-runtime C++ 单元测试的行为。

## 按目标查找

| 目标 | 测试 |
| --- | --- |
| manifest、资源和 package 安全 | `package_preflight_tests.py`、`package_image_fixture_tests.py` |
| app 安装/更新/删除/回滚 | `app_registry_tests.py`、`win32_browser_cli_tests.py` |
| HTML/CSS 表和 profile | `html_support_table_tests.py`、`css_support_table_tests.py`、`render_core_feature_*_tests.py` |
| 视觉 diagnostics 和布局捕获 | `pipeline_visual_diagnostics_tests.py`、`flex_grid_capture_tests.py` |
| 模板和外部作者流程 | `template_trial_tests.py` |
| 字体、链接图和构建切片 | `font_policy_report_tests.py`、`render_core_link_map_tests.py` |

缩小问题时可以直接运行单个 Python 文件；确认修复时优先运行对应 CTest，
因为 CTest 会提供正确的构建 executable 和工作目录。

`package_image_fixture_tests.py` 是一个跨工具验收检查：先对天气样例运行 CLI package
预检并验证 `imageDiagnostics`，再通过 Win32 壳捕获同一个 package，读取 BMP 像素，确保
包内图片不会退化成不可见占位。

`font_policy_report_tests.py` 验证 app 字体路径：检查字体策略样例报告两个可用 `.jffont`
runtime family，保持故意缺字 warning 稳定，并用 Win32 `--use-app-fonts` 捕获样例。

`win32_browser_cli_tests.py` 检查交互式 Win32 壳的 CLI/help/error 契约，包括 registry
安装/更新/回滚/启用/禁用命令模式、授权 file-broker smoke 命令和坏 app 系统存活 smoke。

`jellyframe_cli_external_trial_evidence` 是仅限 Windows 的 CTest。它运行干净目录中的
`jellyframe_cli.py trial` 流程，将官方试用包诊断、预期 capability 拒绝、package 预览与
安装/更新/回滚恢复汇总为可重复的证据目录。

生成的报告和截图应保留在 `build*/test_outputs` 或 `out/`，不要提交这些输出。
