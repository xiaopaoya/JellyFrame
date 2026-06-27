# 工具回归测试

这里保存开发工具层的轻量回归测试，主要锁住 package/check 这类不属于
render-core 或 app-runtime C++ 单元测试的行为。

`package_image_fixture_tests.py` 是一个跨工具验收检查：先对天气样例运行 CLI package
预检并验证 `imageDiagnostics`，再通过 Win32 壳捕获同一个 package，读取 BMP 像素，确保
包内图片不会退化成不可见占位。

生成的报告和截图应保留在 `build*/test_outputs` 或 `out/`，不要提交这些输出。
