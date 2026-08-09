# JellyFrame VS Code 工具

> 最后更新：2026-08-10；适用版本：0.6.0-dev；兼容基线：0.5.0

这是 JellyFrame app package 的薄开发扩展。它不会实现第二套 parser 或 packer；
所有命令都委托给 `tools/jellyframe_cli.py`。

## 功能

- 为 `jellyframe.app.json` 关联 JSON schema。
- 命令面板提供 validate、check、preview、桌面壳调试、frame script 回放、打开截图和生成 package。
- 可从内置 weather、clock、timer 和 calculator 模板创建 app。
- 在专用 `JellyFrame` output channel 中显示 CLI 输出。
- `JellyFrame Report` webview 会优先展示 CLI 的 `developerAdvice[]`，再汇总
  resources、references、warnings 和管线 diagnostics。
- 对 app 作者建议、package warnings 和管线 diagnostics 提供 inline diagnostics。
- Explorer 中的 JellyFrame 状态视图显示当前 app、构建目录、报告诊断和性能摘要。
- 自动发现 `build/Release`、`build/Debug` 以及重命名后的桌面壳，也可在设置中指定路径。
- 可配置仓库根目录、Python 可执行文件、默认 target 和字体预算。

## 开发使用

可以用 VS Code extension development mode 打开本目录；如果从其他位置运行，
将 `jellyframe.repoRoot` 指向 JellyFrame 仓库。扩展优先使用 `build/Release`，
其次使用 `build/Debug`，也可以在设置中覆盖。

使用 `JellyFrame: Show Last Report` 可以重新打开最近一次报告面板。

使用 `JellyFrame: Debug App In Desktop Shell` 进行交互式 app 调试，使用
`JellyFrame: Run Frame Script` 进行确定性回放，使用 `JellyFrame: Open Capture`
打开最近或指定的 BMP/PPM 截图。`JellyFrame: Preview Package` 仍用于 package
预检和伪浏览器截图，并将 JSON 报告关联到诊断。
