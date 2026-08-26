# WS147 ScriptTask Value Frame v4 移植验收要求

## 目的

验证 Developer Image 的 script worker 到 UI task 值帧通路已使用 frame codec v4，并确认变换、祖先 clip、变换层自身的 `overflow` clip、圆角边缘和 stop/relaunch 不会造成静止、漏绘、残影或错误恢复。

本项不是 30 FPS 性能验收；记录帧率和内存，但不以固定帧率作为通过条件。

## 前置条件

- 板卡：WS147，记录串口、屏幕方向、Developer Image identity、Provider version 与 manifest identity。
- 从本次主线构建 Developer Image、Provider 和 manifest；三者必须来自同一构建产物。不得只刷写 `.bin` 后沿用旧 manifest。
- 启用 installed script task runtime，并确认 `ports/esp32s3-idf/main/jellyframe_esp32s3_installed_script_task.cpp` 的 `frame_codec_options().version` 为 `4`。
- 使用 Provider 安装，不得绕过 JFDP/registry 写入 raw partition。
- 串口日志、Provider 事务记录和每项操作的时间戳应完整保存。

## Fixture

1. `jelly_watch_face`：保留现有 JS timer/`setInterval`，每秒改变至少一根指针的 `style.transform`。
2. 新增临时或验收包 `script_transform_clip_v4`：300 x 300 viewport；包含下列画面并以定时器或脚本状态在 1 秒内往返运动。
   - 旋转或缩放元素穿过一个祖先 `overflow: hidden` 的矩形 clip 边界。
   - 旋转元素位于自身 `overflow: hidden` 且带 `border-radius` 的容器中，使其源表面与目标 clip 都被使用。
   - 静态文本、渐变或背景位于变换元素后方，用于观察透明边缘、残影和错误清屏。
   - 一段合法 `scale(0)` 到正常 scale 的切换；`scale(0)` 时该元素消失，但 session、worker 和后续正常帧必须继续工作。

Fixture 可以只用于验收，不要求作为公开示例提交；HTML/CSS/JS、manifest 与包 hash 必须归档。

## 执行

1. 查询 Device Identity，保存 wire identity、manifest identity 和 Provider 输出。若不匹配，停止测试并标记 `blocked`，不得改用旧 manifest 伪造通过。
2. 安装 `jelly_watch_face`，launch 后连续目检 60 秒。至少每 5 秒记录一次日志或 Provider telemetry：`mutation_seq`、`published_seq`、`accepted_seq`、`presents_failed`。
3. stop 后重新 launch 两次；每次都重复至少 15 秒。确认第一帧出现且指针持续运动。
4. 安装并 launch `script_transform_clip_v4`，连续运行 60 秒。分别目检祖先 clip、圆角自身 clip 和 `scale(0)` 往返阶段。
5. 在 fixture 运行期间执行一次 stop/relaunch；若端侧允许，从安装的上一个版本 rollback 后再 launch 一次。不得在复位、重刷或 task reset 后才宣称恢复。
6. 安装一个 v3 编码但带 v4 source-clip 字段的故意畸形 frame fixture，或以端侧受控探针注入等价字节序列。预期为 decode/reject，随后正常 v4 frame 仍可被接受。不得导致 panic、lease 泄漏或后续 frame mailbox 停滞。

## 通过条件

- 两个正常包均完成 install -> launch -> stop -> relaunch；`jelly_watch_face` 指针在每个运行段持续运动。
- `published_seq` 与 `accepted_seq` 持续推进；短暂 mailbox backlog 可以记录，但结束时不得永久停滞。`presents_failed=0`。
- 无缺角、双重裁剪、圆角外溢、背景色块、旧帧残留、闪烁或异常整帧清空。`scale(0)` 仅隐藏目标元素，不中止会话。
- 畸形 v3/v4 边界样本被拒绝，拒绝后同一 session 或新 session 的正常 v4 帧可继续 present。
- 无 watchdog、panic、reset、brownout、heap corruption、DMA/SPI/panel error、failed flush，且 worker/UI task 均经正常 teardown 退出。

## 归档

归档目录包含 `report.md`、`summary.json`、完整 serial/Provider 日志、Developer Image manifest、Provider version/identity、fixture 源码和 `.jfapp` hash。`summary.json` 至少包含：

```json
{
  "result": "pass|partial|fail|blocked",
  "frame_codec_version": 4,
  "watch_face": {"seconds": 60, "mutation_seq": 0, "published_seq": 0, "accepted_seq": 0, "presents_failed": 0},
  "transform_clip": {"seconds": 60, "visual_ok": true, "scale_zero_recovered": true},
  "malformed_v3_rejected": true,
  "relaunches_ok": 0,
  "errors": []
}
```

无法取得图像证据时，可用连续串口/Provider telemetry、完整操作记录与人工目检结论替代；不得把未执行的畸形边界或 relaunch 标记为通过。
