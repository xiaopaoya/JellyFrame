# WS147 已安装 Script App 验收

> 适用范围：Device OS A2；协议：JFDP/1；状态：待执行。

本单验证已安装 `runtime.script: "classic"` App 的真实执行闭环。它不以资源可读取、页面可静态绘制或 desktop shell 成功替代脚本执行证据。

## 前置条件

1. 记录待测 Runtime/Device OS commit、Render Core version/ABI、ESP-IDF version、WS147 board revision、provider version、Developer Image manifest SHA-256 与 COM endpoint。
2. 使用干净的构建目录，Developer Image 必须显式配置脚本 worker：

```powershell
idf.py -B build-ws147-installed-script `
  -D SDKCONFIG_DEFAULTS=sdkconfig.ws147_device_image_lifecycle_acceptance.defaults `
  -D JELLYFRAME_BUILD_SCRIPTING=ON `
  -D JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME=ON build flash
```

3. 归档构建日志及最终 `sdkconfig`。必须确认
   `CONFIG_JELLYFRAME_ESP32S3_ENABLE_SCRIPT_TASK_RUNTIME=y`，且不得启用任何
   `RUN_SCRIPT_*_ACCEPTANCE` fixture。
4. 用仓库内最小 fixture 打包，记录 bundle SHA-256：

```powershell
python tools/package_app.py `
  --root tests/fixtures/apps/jelly_installed_script_input `
  --target rect-172x320 `
  --output-bundle <absolute-artifact-dir>/jelly_installed_script_input.jfapp `
  --report <absolute-artifact-dir>/jelly_installed_script_input.package-report.json
```

该 fixture 初始显示 `Ready: 0`。点击 `Tap` 后依次显示 `Tapped: 1`、`Tapped: 2`、`Tapped: 3`，且按钮由绿色变为黄色。

## 必测流程

| Case | 操作 | 通过条件 |
| --- | --- | --- |
| Identity | `discover -> info`，与 manifest 对照。 | board/profile/image/runtime/core revision/ABI 完全匹配。 |
| Install and launch | 安装 fixture，读取 JSONL terminal，再 launch。 | install/launch 均返回 typed `ok`；屏幕出现 `Ready: 0`，不是静态默认页或空白页。 |
| Touch to script | 对 `Tap` 完成三次完整 down/up。 | 每次可见文本递增且颜色改变；日志中 `input_seq`、`mutation_seq`、`published_seq`、`accepted_seq` 均前进。 |
| Stop and relaunch | `stop` 后重新 `launch`。 | stop 返回 `ok`；再次启动回到 `Ready: 0`，证明旧 JerryScript realm/DOM 没有残留。 |
| Installed-app switch | 保持 fixture A 运行时，安装或启动另一个静态 bundle，再启动 fixture A。 | 无旧帧覆盖新 App；fixture A 再启动后可重新触控。 |
| Controlled script fatal | 使用同结构 fixture，将外部 classic script 改为同步抛出异常后安装并 launch。 | Device OS 记录 `app-runtime-failure`，返回 protected launcher；无 MCU reset、watchdog、panic、registry 损坏。之后原 fixture 仍可 install/launch。 |
| Repetition | 对正常 fixture 执行至少 30 次 `launch -> one tap -> stop`。 | 每轮四段序列均符合输入到呈现的先后关系；无 present failure、DMA/SPI/panel error、watchdog、reset 或 task teardown timeout。 |

## 日志与判定

归档 raw JFDP provider stdout/stderr、CLI command/stdout/stderr、flash/build log、`logs` response、屏幕目检记录及 `summary.json`。日志至少应包含：

```text
JellyFrameInstalledScript: launch prepared ... scripts=1
JellyFrameInstalledScript: worker ... initialized=1
JellyFrameInstalledScript: session stopped ... input_seq=N mutation_seq=N published_seq=N accepted_seq=N presents_failed=0
```

对每次正常触控，最终应满足：

```text
input_seq >= 1
mutation_seq >= 1
published_seq >= mutation_seq
accepted_seq >= published_seq
presents_failed = 0
```

若 frame mailbox 满载而发生一次暂时发布重试，可接受 `published_seq` 晚于同一次输入；不得丢失最终可见更新。任何 `initialized=0`、fatal、任务等待超时、序列不前进、默认静态页、或 panel/DMA/SPI/watchdog/reset 异常均为失败，必须保留原始证据并维持 A2 `partial`。

## 非目标

本轮不验证网络、真实 service provider、Canvas host binding、全屏渐变 30 FPS 或对外 Developer Image 发布。它仅关闭“已安装 classic Script App 的输入、value-frame、呈现与受控故障恢复”这一 A2 子路径。
