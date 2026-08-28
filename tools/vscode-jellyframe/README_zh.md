# JellyFrame VS Code 工具

> 最后更新：2026-08-29；适用版本：0.6.0-dev；扩展版本：0.4.29；兼容基线：0.5.0

JellyFrame Tools 是面向 App 作者的 VS Code 扩展，让你在编辑器里检查、预览、调试和
打包 JellyFrame App。安装后可以从左侧 JellyFrame 活动栏、资源管理器/编辑器右键菜单
或命令面板开始工作。

## 功能

- 为 `jellyframe.app.json` 关联 JSON schema。
- 命令面板提供“验证 App 包结构”“检查 App 包渲染”“预览”、VS Code 内嵌调试、外部窗口调试、frame script 回放、打开截图和生成 package。
- 可从内置 blank、weather、clock、timer 和 calculator 模板创建 app。
- 在专用 `JellyFrame` output channel 中显示 CLI 输出。
- `JellyFrame Report` webview 会优先展示 CLI 的 `developerAdvice[]`，再汇总
  resources、references、warnings 和管线 diagnostics。
- 对 app 作者建议、package warnings 和管线 diagnostics 提供 inline diagnostics。
- Explorer 中的 JellyFrame 状态视图显示当前 app、构建目录、报告诊断和性能摘要。
- 首次配置作者环境时选择已安装的 JellyFrame SDK，独立 App 工作区随后可直接使用。
- 可从 GitHub 最新发布下载并安装经过 SHA-256 校验的 App 作者 SDK，不覆盖已有目录。
- 自动发现 SDK 的桌面壳构建，也可在设置中指定路径。
- 可配置 SDK 根目录、Python 可执行文件、默认 target 和字体预算。
- 在 VS Code 左侧提供始终可见的 `JellyFrame` 活动栏视图，集中显示 App、构建、报告、
  诊断和性能操作；对 `jellyframe.app.json` 以及 HTML/CSS 文件提供针对性的右键菜单。
- Device OS 生命周期操作按 capability 显式门控；未获当前选中 provider 声明支持时不会显示。

## 使用扩展

仓库当前提供的是源码版扩展，尚未发布到 VS Code Marketplace。面向 App 作者的使用方式是：

1. 安装扩展后打开独立的 App 工作区。
2. 点击左侧 JellyFrame 视图的“作者环境：未配置”。选择“从 GitHub 下载 App 作者 SDK”时，
   扩展会从官方 GitHub Release 下载唯一的 SDK ZIP，校验 SHA-256 后安全解压并自动配置环境；
   已经安装 SDK 时选择“选择已安装的 JellyFrame SDK”。
3. 配置完成后，“作者环境”会显示 SDK 版本。点击它可检查更新、切换 SDK 或在资源管理器中打开 SDK。
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
SDK 下载仅接受 `https://github.com/xiaopaoya/JellyFrame` 的最新 Release，并要求 Release
提供 GitHub SHA-256 摘要或同名 `.sha256` 校验文件；下载失败或校验缺失时不会安装。安装到仓库之外时，扩展依次使用项目 `.jellyframe/project.json`、已配置的 SDK、
`JELLYFRAME_SDK_ROOT` 或从当前工作区向上找到的 SDK；`jellyframe.sdkRoot` 是推荐的显式
设置，`jellyframe.repoRoot` 仅保留为旧别名。`jellyframe.buildDir` 可选，用于指定桌面运行目录。
当 App 操作需要 SDK 而尚未配置时，扩展会直接提供“配置作者环境”，不会继续执行缺少工具的命令。
SDK 安装不会覆盖已有目录；Windows 的短暂权限或文件占用会自动重试，仍失败时可选择重试、其他位置，或使用已验证的现有 SDK。
扩展优先使用 SDK 中的 `build/desktop-release/Release`，其次使用 `build/desktop-debug/Debug`。
如果 App manifest 声明了 `runtime.script`，未显式设置 `jellyframe.buildDir` 时扩展只会使用
`build/desktop-scripting-release/Release` 或 `build/desktop-scripting-debug/Debug`。所选构建必须启用
`JELLYFRAME_BUILD_SCRIPTING=ON`；仍保留 1.0 前
`JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME` cache 项的构建会被拒绝，并提示重新配置，而不会被意外运行。
缺少兼容桌面构建时，错误提示操作和“环境”分组都会提供“创建兼容桌面构建”。在作者明确点击后，它会在本机配置并构建
受管理的 Release profile；仓库中已有 JerryScript 源码但尚未生成库时，会先构建该依赖。该命令不会下载第三方源码，
也不会删除自定义构建目录。独立 App 的报告、截图和临时 package 输出会写入 App 自己的
`.jellyframe/build`，不会污染 SDK。

使用 `JellyFrame: Show Last Report` 可以重新打开最近一次报告面板。

“验证 App 包”是快速的纯包门禁：检查 manifest、入口、资源、引用和声明预算，适合频繁运行，
不会启动 Render Core，也不会询问分辨率、测量布局、帧时间或真实设备性能。
“检查 App 渲染”会先做同样的结构验证，再从仓库 preset 和当前 App manifest 已声明 target 中选择目标 profile，运行 Render Core 预检、响应式布局和字体检查；
该入口还可以附加 `.jfcapture` 程控回放，把静态管线诊断和多页面交互路径合并到一份报告。
需要查看实际画面或手动交互时，请使用“预览”或桌面调试。

`JellyFrame` 活动栏视图只使用一层顶级分区；每个 App 操作、构建状态和设备状态直接显示在对应分区下，
避免 VS Code 树控件的多层缩进造成层级误读。命令以图标和功能提示表示，构建、设备与报告结果则为只读状态，避免混淆。它不依赖当前是否打开编辑器
或工作区文件。安装新版 VSIX 后，如果旧扩展实例仍在运行，请执行一次“Developer: Reload Window”
（开发人员：重新加载窗口）。资源管理器中
右键 `jellyframe.app.json`，或在 App 的 HTML/CSS/manifest 文件编辑器中右键，可以
直接使用常用操作。这些入口与命令面板调用同一组命令，输出和诊断行为一致。

“从模板新建 App”使用目录选择器确定存放位置，并根据 App 目录名建议一个 `org.example.*`
标识。只有需要已有组织命名空间时才选择“指定 App ID”；自定义 ID 必须以字母或数字开头，且只能包含
字母、数字、点、连字符和下划线。创建 App 时的 target 选择器仅列出已识别的仓库 preset，因此生成的
manifest 可以直接打包。

使用“在 VS Code 中调试 App”会打开一个编辑器标签页：它启动独立的隐藏桌面壳会话，将完整的、单序号
viewport 帧快照送入标签页，并把点击、拖动、滚轮和常用按键转回该会话。视窗栏可选择 App 默认、常用设备
尺寸，或输入 `64..2048` 的自定义宽高；应用尺寸会重启桌面壳，因此 CSS media query 与布局都会按请求的
尺寸重新执行，首帧回传的实际尺寸仍是权威结果。Stop 后标签页会保留并显示“继续调试”和“重新启动”；关闭
标签页才会请求壳退出。若壳未在短暂宽限期内退出，扩展会终止该调试进程树。它不复用外部窗口、截图或其他
会话的 framebuffer。需要检查原生窗口行为时，使用“在外部窗口调试 App”。

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
`jellyframe-ws147-developer-0.6.0-a2-provider-0.1.1-dev.zip`。
首次使用前，请安装独立 provider 并在 JellyFrame 设置中填写其可执行文件绝对路径；如果路径
缺失或无效，命令会直接给出配置提示。先执行“发现设备”，再使用“读取设备身份”按配置的
Developer Image manifest 校验所选端点；“列出已安装 App”会显示同一端点的 registry generation、版本、
状态和回滚可用性。这三项均为只读操作，不会安装、启动、删除或刷写设备。
部署时会选择 viewport 与已验证设备 display 匹配的唯一 App target；target 名称可以不同于设备 profile，
但所有同尺寸 target 仍必须唯一。没有这个无歧义声明的 App 不会针对该设备打包或安装。

最初的 WS147 `0.1.0-dev` provider 继续保持只读，因此活动栏会隐藏 lifecycle operation。已交付的
`0.1.1-dev` provider 通过 `capabilities.supportedOperations` 声明已验证的 lifecycle operation；发现后活动栏只会
显示相符的部署、启动、停止、回滚、删除、App 日志和恢复状态入口。这是一道显式安全门，而不是声称所有声明操作已在
每台设备上被接受。部署与删除始终要求确认，扩展会将 typed terminal result 记录到“设备状态”区。
