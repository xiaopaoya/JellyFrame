# JellyFrame VS Code 工具

> 最后更新：2026-08-25；适用版本：0.6.0-dev；兼容基线：0.5.0

JellyFrame Tools 是面向 App 作者的 VS Code 扩展，让你在编辑器里检查、预览、调试和
打包 JellyFrame App。安装后可以从左侧 JellyFrame 活动栏、资源管理器/编辑器右键菜单
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
- 自动发现 `build/desktop-release/Release`、`build/desktop-debug/Debug` 以及桌面壳，
  也可在设置中指定路径。
- 可配置仓库根目录、Python 可执行文件、默认 target 和字体预算。
- 在 VS Code 左侧提供始终可见的 `JellyFrame` 活动栏视图，集中显示 App、构建、报告、
  诊断和性能操作；对 `jellyframe.app.json` 以及 HTML/CSS 文件提供针对性的右键菜单。
- Device OS 生命周期操作按 capability 显式门控；未获当前选中 provider 声明支持时不会显示。

## 使用扩展

仓库当前提供的是源码版扩展，尚未发布到 VS Code Marketplace。最简单的试用方式是：

1. 先在 JellyFrame 仓库根目录完成一次桌面 Release 构建，生成
   `build/desktop-release/Release`。
2. 用 VS Code 打开 `tools/vscode-jellyframe` 文件夹。
3. 按 `F5`，在新打开的 Extension Development Host 窗口中打开一个 JellyFrame 仓库，
   或将 `jellyframe.repoRoot` 设置为仓库根目录。
4. 点击左侧活动栏中的 JellyFrame 图标开始操作；也可以打开 `jellyframe.app.json`、App
   的 HTML/CSS 文件后使用右键菜单。

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

脚本优先使用 Node.js 的 `vsce`、`npx`（或可用的 `pnpm`）打包；这些工具均不可用时，会自动使用内置 VSIX 打包器。
安装或更新仍需要 VS Code 的 `code` 命令在 PATH 中；也可以通过 `-CodeCommand` 传入完整 CLI 路径（通常是
`...\Microsoft VS Code\bin\code.cmd`）。若传入 `Code.exe` 且相邻 CLI 存在，脚本会自动改用该 CLI。若 PowerShell 阻止本地脚本，可在当前窗口执行
`Set-ExecutionPolicy -Scope Process Bypass`。手动安装时，也可以在 VS Code 的扩展视图中选择“从 VSIX 安装”。
安装到仓库之外时，扩展会优先从当前工作区向上寻找仓库；仍可在设置中填写 `jellyframe.repoRoot`；`jellyframe.buildDir`
可选，用于指定桌面运行目录。扩展优先使用 `build/desktop-release/Release`，其次使用
`build/desktop-debug/Debug`。
如果 App manifest 声明了 `runtime.script`，未显式设置 `jellyframe.buildDir` 时扩展只会使用
`build/desktop-scripting-release/Release` 或 `build/desktop-scripting-debug/Debug`。所选构建必须启用
`JELLYFRAME_BUILD_SCRIPTING=ON`；仍保留 1.0 前
`JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME` cache 项的构建会被拒绝，并提示重新配置，而不会被意外运行。

使用 `JellyFrame: Show Last Report` 可以重新打开最近一次报告面板。

## Device OS Provider

开发板专用的 Developer Image provider 需要单独安装。在设置中将
`jellyframe.deviceProvider` 设为其 `jellyframe-device.cmd` 的绝对路径，将
`jellyframe.deviceManifest` 设为匹配的 Developer Image manifest。先运行
“JellyFrame：发现设备”，再运行“JellyFrame：读取设备身份”核对所选 endpoint 的 wire
identity。扩展只会经 CLI 调用已配置的 provider；不会打包 provider、扫描串口或提供串口 fallback。

“验证 App 包”是快速的纯包门禁：检查 manifest、入口、资源、引用和声明预算，适合频繁运行，
不会启动 Render Core，也不会询问分辨率、测量布局、帧时间或真实设备性能。
“检查 App 渲染”会先做同样的结构验证，再选择目标 viewport，运行 Render Core 预检、响应式布局和字体检查；
该入口还可以附加 `.jfcapture` 程控回放，把静态管线诊断和多页面交互路径合并到一份报告。
需要查看实际画面或手动交互时，请使用“预览”或桌面调试。

`JellyFrame` 活动栏视图将“检查与预览”“交互式调试”“创建与自动化”分组；命令以图标和功能提示表示，
构建、设备与报告结果则为只读状态，避免混淆。它不依赖当前是否打开编辑器
或工作区文件。安装新版 VSIX 后，如果旧扩展实例仍在运行，请执行一次“Developer: Reload Window”
（开发人员：重新加载窗口）。资源管理器中
右键 `jellyframe.app.json`，或在 App 的 HTML/CSS/manifest 文件编辑器中右键，可以
直接使用常用操作。这些入口与命令面板调用同一组命令，输出和诊断行为一致。

使用“在 VS Code 中调试 App”会打开一个编辑器标签页：它启动独立的隐藏桌面壳会话，将完整的、单序号
viewport 帧快照送入标签页，并把点击、拖动、滚轮和常用按键转回该会话。标签页的 Stop 按钮或关闭标签页
都会请求壳退出；若壳未在短暂宽限期内退出，扩展会终止该调试进程树。它不复用外部窗口、截图或其他会话的
framebuffer。需要检查原生窗口行为时，使用“在外部窗口调试 App”。

内嵌调试器还提供 Record 按钮，用于创建语义化 <code>.jfcapture</code>：开始录制，
操作 App，停止录制后选择保存位置。录制期间 Live log 自动切到“事件”，记录稳定的控件操作，
而不是像素坐标：

~~~text
event 3 click-id notifications
event 3 set-checked notifications 1
event 7 click-id brightness
event 7 set-value brightness 72
~~~

请为需要录制的 button、input、select 提供唯一的 ASCII <code>id</code>。这样即使间距、缩放或
布局发生变化，capture 仍可回放。向导有意不录制滚动、自由 Canvas 手势和没有稳定 id 的控件；
这些场景仍应手写 pointer/wheel 事件。保存后可通过“运行帧脚本”执行，也可以在“检查 App 渲染”
中选择程控回放。

使用“运行帧脚本”进行确定性回放，使用“打开截图”打开最近或指定的 BMP/PPM 截图。
`JellyFrame：预览 App 包` 会执行 package 预检、生成独立
JSON 报告并自动打开截图。验证、检查和预览分别保留自己的报告，不会互相覆盖。

“发现设备”只连接已配置的 Device OS provider，不会猜测串口或 USB 端点。扩展不捆绑板卡专属
provider；WS147 请安装版本化交付包
`jellyframe-ws147-developer-<image-version>-provider-0.1.1-dev.zip`。
首次使用前，请安装独立 provider 并在 JellyFrame 设置中填写其可执行文件绝对路径；如果路径
缺失或无效，命令会直接给出配置提示。先执行“发现设备”，再使用“读取设备身份”按配置的
Developer Image manifest 校验所选端点；“列出已安装 App”会显示同一端点的 registry generation、版本、
状态和回滚可用性。这三项均为只读操作，不会安装、启动、删除或刷写设备。
部署时会选择名称与尺寸均匹配已验证设备 profile 的 App target；profile 名称不同时，只接受唯一一个
同尺寸 manifest target。没有这个无歧义声明的 App 不会针对该设备打包或安装。

WS147 provider `jellyframe-device@0.1.1-dev` 只会为已验证的 `rect-172x320` Developer Image 声明
lifecycle operation。活动栏从 `capabilities.supportedOperations` 推导可见的部署、启动、停止、回滚、App 日志和
恢复状态入口；字段缺失或为空时，UI 保持只读。部署与删除始终需要确认，扩展会在设备状态中记录 typed terminal
result。
