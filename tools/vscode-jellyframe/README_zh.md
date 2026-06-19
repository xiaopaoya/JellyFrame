# JellyFrame VS Code 工具

这是 JellyFrame app package 的薄开发扩展。它不会实现第二套 parser 或 packer；
所有命令都委托给 `tools/jellyframe_cli.py`。

## 功能

- 为 `jellyframe.app.json` 关联 JSON schema。
- 命令面板提供 validate、check、preview、打开交互式 Win32 browser 和生成 package。
- 可从内置 weather、clock、timer 和 calculator 模板创建 app。
- 在专用 `JellyFrame` output channel 中显示 CLI 输出。
- `JellyFrame Report` webview 会汇总最近一次 package report、resources、
  references、warnings 和管线 diagnostics。
- 对需要处理的 package warnings 和管线 diagnostics 提供 inline diagnostics。
- 可配置仓库根目录、Python 可执行文件、默认 target 和字体预算。

## 开发使用

可以用 VS Code extension development mode 打开本目录；如果从其他位置运行，
将 `jellyframe.repoRoot` 指向 JellyFrame 仓库。扩展默认使用正常构建后的
`build/Release` 工具。

使用 `JellyFrame: Show Last Report` 可以重新打开最近一次报告面板。

使用 `JellyFrame: Open In Win32 Browser` 进行交互式 app 调试。使用
`JellyFrame: Preview Package` 进行确定性的伪浏览器截图；preview 也会写出 JSON report
并更新报告面板。
