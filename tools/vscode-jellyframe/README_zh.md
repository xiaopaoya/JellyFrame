# JellyFrame VS Code 工具

> 最后更新：2026-08-10；适用版本：0.6.0-dev；兼容基线：0.5.0

JellyFrame Tools 是面向 App 作者的 VS Code 扩展，让你在编辑器里检查、预览、调试和
打包 JellyFrame App。安装后可以从顶部的 JellyFrame 菜单、资源管理器/编辑器右键菜单
或命令面板开始工作。

## 功能

- 为 `jellyframe.app.json` 关联 JSON schema。
- 命令面板提供“验证 App 包结构”“检查 App 包渲染”“预览”、VS Code 内嵌调试、外部窗口调试、frame script 回放、打开截图和生成 package。
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

如果希望像普通扩展一样安装或更新，可以直接在扩展目录运行统一脚本：

```powershell
.\manage-extension.ps1
```

默认动作是重新打包并强制更新当前安装的扩展。也可以只打包，或执行普通安装：

```powershell
.\manage-extension.ps1 -Action Package
.\manage-extension.ps1 -Action Install
.\manage-extension.ps1 -Action Update
```

脚本需要 Node.js 的 `npx`（或可用的 `pnpm`）和 VS Code 的 `code` 命令在 PATH 中；也可以通过
`-NpxCommand` 或 `-CodeCommand` 传入完整路径。若 PowerShell 阻止本地脚本，可在当前窗口执行
`Set-ExecutionPolicy -Scope Process Bypass`。手动安装时，也可以在 VS Code 的扩展视图中选择“从 VSIX 安装”。
安装到仓库之外时，在设置中填写 `jellyframe.repoRoot`；`jellyframe.buildDir`
可选，用于指定桌面构建目录。扩展优先使用 `build/Release`，其次使用 `build/Debug`。
如果 App manifest 声明了 `runtime.script`，未显式设置 `jellyframe.buildDir` 时会优先寻找
`build/scripting-ci-local` 或其他脚本构建。

使用 `JellyFrame: Show Last Report` 可以重新打开最近一次报告面板。

“验证 App 包”是快速的纯包门禁：检查 manifest、入口、资源、引用和声明预算，适合频繁运行，
不会启动 Render Core，也不会询问分辨率、测量布局、帧时间或真实设备性能。
“检查 App 渲染”会先做同样的结构验证，再选择目标 viewport，运行 Render Core 预检、响应式布局和字体检查；
该入口还可以附加 `.jfcapture` 程控回放，把静态管线诊断和多页面交互路径合并到一份报告。
需要查看实际画面或手动交互时，请使用“预览”或桌面调试。

顶部的 `JellyFrame` 菜单按“包检查、调试、报告”分组提供完整工作流。资源管理器中
右键 `jellyframe.app.json`，或在 App 的 HTML/CSS/manifest 文件编辑器中右键，可以
直接使用常用操作。这些入口与命令面板调用同一组命令，输出和诊断行为一致。

使用“在 VS Code 中调试 App”会打开一个编辑器标签页：它启动独立的隐藏桌面壳会话，将完整的、单序号
viewport 帧快照送入标签页，并把点击、拖动、滚轮和常用按键转回该会话。标签页的 Stop 按钮或关闭标签页
都会请求壳退出；若壳未在短暂宽限期内退出，扩展会终止该调试进程树。它不复用外部窗口、截图或其他会话的
framebuffer。需要检查原生窗口行为时，使用“在外部窗口调试 App”。

使用“运行帧脚本”进行确定性回放，使用“打开截图”打开最近或指定的 BMP/PPM 截图。
`JellyFrame：预览 App 包` 会执行 package 预检、生成独立
JSON 报告并自动打开截图。验证、检查和预览分别保留自己的报告，不会互相覆盖。
