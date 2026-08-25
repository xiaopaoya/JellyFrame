# WS147 Provider 与 VS Code 冒烟验收

> 日期：2026-08-25；适用：`jellyframe-device@0.1.0-dev`、JellyFrame VS Code Tools `0.4.7`；范围：只读发现与身份校验

## 目的

验证独立交付的 WS147 provider 能由 VS Code 配置并通过 JellyFrame CLI 正确调用。该验收只执行
`discover` 与 `info`；不安装、启动、停止、删除、回滚 App，也不刷写或擦除板卡。

## 输入

- `jellyframe-ws147-developer-0.6.0-a2-provider-0.1.0-dev.zip`
- 已烧录并启动的匹配 WS147 Developer Image。
- JellyFrame VS Code Tools `0.4.7` 或更新版本。
- Python 3.10+；安装包内 `provider/requirements.txt` 声明的 `pyserial==3.5`。

## 步骤

1. 将 ZIP 解压到稳定的本地目录。校验其 `SHA256SUMS.txt`；不要只校验 ZIP 文件名。
2. 为该目录创建或选择一个 Python 环境，并安装 `provider/requirements.txt`。
3. 将 `provider/jellyframe-device.config.example.json` 复制为同目录的
   `provider/jellyframe-device.config.json`。只填写当前板卡的 `port`；保留相对 `manifest` 路径，
   并使用不包含端口、路径或密钥的 `endpointId`。
4. 在 VS Code 设置中填写：
   - `jellyframe.deviceProvider`：解压目录下 `provider/jellyframe-device.cmd` 的绝对路径。
   - `jellyframe.deviceManifest`：解压目录下
     `developer-image/ws147-developer-image.manifest.json` 的绝对路径。
5. 重载 VS Code 窗口。在 JellyFrame 侧栏选择“发现设备”。
6. 确认发现结果恰有当前 WS147 endpoint，`connected=true`，并选择“读取设备身份”。
7. 保存 JellyFrame Output channel 的完整命令、stdout 和 stderr；不要混入普通串口监视器输出。

## 通过条件

- 两个插件命令均以退出码 `0` 完成。
- `discover` 的 `provider.id` 为 `jellyframe-device`、版本为 `0.1.0-dev`，且返回一个已连接 WS147。
- `info` 同时返回 `device` 与 `identity`，并与 manifest 一致：
  - `imageVersion=0.6.0-a2`
  - `profileId=rect-172x320`
  - `renderCoreVersion=0.6.1`
  - `renderCoreAbi=1`
  - 显示为 `172x320`、`rect`
- 侧栏显示已连接设备数和读取到的 `0.6.1 / ABI 1` 身份摘要。
- provider/CLI stderr 不含未解释错误；无 reset、watchdog 或 JFDP transport error。

## 失败归因

- provider 路径或 Python 依赖错误：provider 安装/环境问题。
- `transport-unavailable`：端口占用、连线、USB 驱动或板端 transport 问题。
- manifest mismatch：使用了错误的 Developer Image、manifest 或 provider 配置。
- JSON/JSONL shape、request ID 或 operation mismatch：provider/host contract 问题。
- VS Code 无法显示发现结果但 CLI 成功：插件 session/UI 问题。

## 归档

创建 `test_artifacts/ws147-provider-vscode-smoke-20260825/`，至少保存 `report.md`、
`summary.json`、provider 原始 stdout/stderr、JellyFrame Output、使用的 manifest SHA-256、
provider ZIP SHA-256 与 VS Code/插件版本。该冒烟验收不替代后续 install、live log 或
panel/input A2 验收。
