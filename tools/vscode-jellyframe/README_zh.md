# JellyFrame VS Code 工具

> 最后更新：2026-08-10；适用版本：0.6.0-dev；兼容基线：0.5.0

JellyFrame Tools 是面向 App 作者的 VS Code 扩展，让你在编辑器里检查、预览、调试和
打包 JellyFrame App。安装后可以从顶部的 JellyFrame 菜单、资源管理器/编辑器右键菜单
或命令面板开始工作。

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
- 在 VS Code 顶部提供 `JellyFrame` 菜单；对 `jellyframe.app.json` 以及 HTML/CSS
  文件提供针对性的右键菜单。

## 使用扩展

仓库当前提供的是源码版扩展，尚未发布到 VS Code Marketplace。最简单的试用方式是：

1. 先在 JellyFrame 仓库根目录完成一次 Release 构建，生成 `build/Release`。
2. 用 VS Code 打开 `tools/vscode-jellyframe` 文件夹。
3. 按 `F5`，在新打开的 Extension Development Host 窗口中打开一个 JellyFrame 仓库，
   或将 `jellyframe.repoRoot` 设置为仓库根目录。
4. 打开 `jellyframe.app.json`、App 的 HTML/CSS 文件，或直接点击顶部的 `JellyFrame`
   菜单开始操作。

如果希望像普通扩展一样安装，可以在扩展目录执行：

```powershell
npx @vscode/vsce package
```

然后在 VS Code 的扩展视图中打开“更多操作”菜单，选择“从 VSIX 安装”，选中生成的
`.vsix` 文件。安装到仓库之外时，在设置中填写 `jellyframe.repoRoot`；`jellyframe.buildDir`
可选，用于指定桌面构建目录。扩展优先使用 `build/Release`，其次使用 `build/Debug`。

使用 `JellyFrame: Show Last Report` 可以重新打开最近一次报告面板。

顶部的 `JellyFrame` 菜单按“包检查、调试、报告”分组提供完整工作流。资源管理器中
右键 `jellyframe.app.json`，或在 App 的 HTML/CSS/manifest 文件编辑器中右键，可以
直接使用常用操作。这些入口与命令面板调用同一组命令，输出和诊断行为一致。

使用“在桌面壳中调试 App”进行交互式 App 调试，使用“运行帧脚本”进行确定性回放，使用“打开截图”
打开最近或指定的 BMP/PPM 截图。`JellyFrame：预览 App 包` 会执行 package 预检、生成独立
JSON 报告并自动打开截图。验证、检查和预览分别保留自己的报告，不会互相覆盖。
