# 工具回归测试

> 最后更新：2026-07-07；适用版本：0.6.0-dev

这里保存开发工具层的轻量回归测试，主要锁住 package/check 这类不属于
render-core 或 app-runtime C++ 单元测试的行为。

`package_image_fixture_tests.py` 是一个跨工具验收检查：先对天气样例运行 CLI package
预检并验证 `imageDiagnostics`，再通过 Win32 壳捕获同一个 package，读取 BMP 像素，确保
包内图片不会退化成不可见占位。

`font_policy_report_tests.py` 验证 app 字体路径：检查字体策略样例报告两个可用 `.jffont`
runtime family，保持故意缺字 warning 稳定，并用 Win32 `--use-app-fonts` 捕获样例。

`win32_browser_cli_tests.py` 检查交互式 Win32 壳的 CLI/help/error 契约，包括 registry
安装/更新/回滚命令模式、授权 file-broker smoke 命令和坏 app 系统存活 smoke。

生成的报告和截图应保留在 `build*/test_outputs` 或 `out/`，不要提交这些输出。
