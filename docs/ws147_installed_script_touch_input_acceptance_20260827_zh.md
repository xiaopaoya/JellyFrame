# WS147 已安装 Script App 触控输入验收

> 最后更新：2026-08-27；适用版本：0.6.0-dev

## 目的

验证已安装 classic-script App 的物理触控事件以纯值 input packet 进入 worker，触发 JS/DOM
mutation、发布新 value frame 并完成 panel present。本项补充已关闭的 v4 timer-driven
render-output 验收；它不重测完整 JFDP lifecycle，也不是 Canvas 或帧率基准。

## 前置条件

- WS147 Developer Image、Provider 与 manifest 必须来自同一构建；保存 `discover`、`info` 与
  wire identity。image ID、profile、version、Render Core ABI/version 或 source revision 任一不匹配时
  标为 `blocked`，不得替换旧 manifest 继续测试。
- 启用 installed script task runtime，使用 `rect-172x320` profile。记录 firmware SHA-256、Provider
  version、COM endpoint、ESP-IDF 版本和 SDKCONFIG。
- Provider 独占设备端点；不得并行使用原始 serial monitor。

## Fixture

通过 Provider 安装一个最小 classic-script `.jfapp`，至少包含：

1. 一个可点击按钮。每次 `click` 递增可见计数文本。
2. 一个 `input[type=range]`。其 `input` handler 同时更新可见数值和宽度、颜色或进度条，便于在
   拖动期间确认连续重绘。
3. 静态标题和背景，便于观察局部残留、闪烁或错误清屏。

归档 fixture HTML/CSS/JS、`jellyframe.app.json`、`.jfapp` 和 package hash。应用不得用 timer
伪造值变化；按钮和 range 的可见变化必须只由对应 input handler 产生。

## 执行

1. 安装并 launch fixture，等待 worker ready 与首帧 present。保存初始 Provider logs。
2. 在按钮内连续点击至少三次。每次确认计数恰好递增一次，且没有整帧闪烁或旧文本残留。
3. 在 range 轨道上从低值拖至高值，再反向拖回。至少在起点、中点、终点各目检一次可见数值和
   进度显示；拖动期间应反映当前指针位置，而不是松手后才更新。
4. 读取 app-scoped `script-input` 与 `script` logs，保存同一运行段中的 `posted`、`worker_seq`、
   `mutation_seq`、`published_seq`、`accepted_seq` 和 `presents_failed`。
5. stop 后重新 launch 一次，重复一次按钮点击和一次 range 拖动。旧 session 的输入不得影响新
   session；新 session 必须从首帧正常进入交互。
6. 可选压力样本：连续拖动或点击至少 20 次。该样本只检查有界队列与恢复，不作 FPS 声明。

## 通过条件

- 按钮 click 和 range drag 均触发对应的 JS 可见变化；range move 必须以带 primary
  `button/buttons` 状态的 value packet 进入 worker，使拖动更新而非仅处理 down/up。
- `posted` 与 `worker_seq` 在交互后推进；引起页面状态变化的操作还必须推进
  `mutation_seq`、`published_seq` 和 `accepted_seq`。允许固定槽 mailbox 的短暂背压，但最终不得永久停滞。
- 正常交互期间 `rejected=0`、`unsupported=0`、`queue_dropped=0`、`presents_failed=0`。
  若任一计数非零，报告必须给出其 event kind、operation 与恢复结果，不能只报告视觉正常。
- 无 watchdog、panic、reset、brownout、DMA/SPI/panel error、failed flush 或 app-to-app 输入串扰。
- stop/relaunch 后新 session 的交互正常，且不会消费停止前遗留的 input packet。

## 归档

归档 `report.md`、`summary.json`、Provider JSON/JSONL、完整 app-scoped logs、manifest、fixture
源码与 `.jfapp` hash。summary 至少记录 identity match、按钮点击数、range 目检结果、各 input
计数、worker/mutation/published/accepted sequence、failed present、relaunch 结果与 errors。未执行物理
触控、只通过 desktop shell 或只观察 timer animation 时，结果必须是 `blocked` 或 `partial`，不能将 v4
render-output 报告复用为本项通过证据。
