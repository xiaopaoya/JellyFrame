# 脚本 App 任务隔离接入指南

> 最后更新：2026-08-15；适用版本：0.6.0-dev；状态：P3-1 至 P3-4 实机验收已关闭

本指南约束 RTOS port 如何接入可选的 script-task runtime。它只描述平台无关接口的使用顺序；创建
任务、CPU affinity、DMA、panel、网络 worker 和 watchdog 仍属于 `ports/`。

## 编译门控

仅当以下两个开关同时为 `ON` 时才能创建脚本 worker：

```text
JELLYFRAME_BUILD_SCRIPTING=ON
JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME=ON
```

此时链接 `jellyframe_script_task_runtime`。任一开关关闭时，不得为 script worker 保留 stack、mailbox、
JerryScript heap 或 service bridge；普通 firmware 的行为与体积不应改变。

## 所有权

| Owner | 可持有 | 不可持有 |
| --- | --- | --- |
| supervisor task | `ScriptTaskSupervisor`、`AppRuntimeHost`、`ScriptTaskServiceBridge`、session、host handle table | `Node*`、JerryScript value、layer/display 指针、framebuffer/panel 指针 |
| script worker | private DOM/realm/timer、`InputController`、`ScriptTaskAppFramePublisher`、`ScriptTaskServiceCompletionSink` | `AppRuntimeHost*`、service record、UI renderer、DMA/panel object |
| UI task | accepted `ScriptTaskAppFrame` value、presentation renderer、framebuffer/panel、原始输入 | worker DOM/realm/wrapper、host service queue |

所有 mailbox payload 都必须是值副本。frame 只能通过 session-scoped sealed lease ID 传递；service request
必须走专用 `service_request_mailbox`；service completion 与 input 共用 worker inbox；frame 不得放入这两个
service 通道。

## 建议循环

supervisor 每一帧或每个 scheduler tick：

```cpp
const auto submitted = bridge.pump_service_requests();
// port-owned workers consume AppRuntimeHost request queue and push completions.
const auto completed = bridge.pump(host_frame_scratch);
```

`submitted` 的 rejection counters 必须进入 app telemetry。host 拒绝一个已经接受到 wire mailbox 的请求时，
bridge 会把终态 `ServiceCompletion` 放入 worker inbox；它不能被静默丢弃。

worker 取消请求只能调用 `services.cancel(requestId)`。该调用产生固定 12-byte 的 `ServiceCancel`
value packet，port 不得让 worker 直接持有 `ScriptTaskServiceBridge`、host job ID 或 provider 指针。
supervisor task 必须优先 drain request/cancel mailbox：queued job 应被移除并退休 token，in-flight job
应保留 cancellation tombstone，迟到 completion 必须释放 provider/host 资源且不得投递给 JS。

真实 completion 若带 host handle，构造 bridge 时必须提供 `max_service_payload_bytes`、
`payload_copy` 与 `payload_release`。copy callback 只能经 `ScriptTaskServicePayloadWriter` 写入上限内的
字节；release callback 必须在复制成功、复制失败、取消、陈旧 completion 与 teardown 时恰好一次清理服务
provider record 和 host-table entry。worker completion sink 收到的是 `payload_lease_id`，使用
`take_script_task_service_payload()` 复制并立即释放；它绝不能解释或保存 host handle。
bridge 会在调用 copy/release callback 前验证 result handle 仍存在、属于 completion 的 app，并在 handle
声明 token 时匹配该 consumer。验证失败时不会调用 callback、不会释放其他 consumer 的资源，而是向 worker
投递 `Failed`（`errorCode=HandleRejected`）终态值。两个 callback 都不得重入或保存 bridge。
若该 sealed lease 已失效，worker runtime 仍必须以空 payload 向已登记的 JS callback 交付一次
`Failed` completion（`errorCode=LeaseRejected`），不能遗留一个永久等待的 callback。

`JerryScriptRuntimeOptions::max_service_callbacks` 是 worker-local callback 的独立上限，默认 16。
port 应将其设为不大于 `service_request_mailbox.max_packets` 与 bridge/service tombstone 容量中的最小值；
超限请求在 worker 内被拒绝，不得产生 outbound request packet。运行时统计的
`service_callback_count` 可用于确认 callback 在 completion、cancel 或 worker stop 后归零。

script worker：

```cpp
take_and_dispatch_script_task_worker_packet(
    supervisor, session, private_input_controller, private_completion_sink, input_limits);
// after input, timer or completion mutates private DOM:
publisher.publish(supervisor, session, make_script_task_app_frame(private_layers, viewport));
```

UI task：

```cpp
ScriptTaskAppFrame frame;
if (take_script_task_app_frame(supervisor, session, frame_limits, frame) ==
    ScriptTaskAppFrameTakeStatus::Accepted) {
    // Render this copied DisplayList only in the UI task.
}
```

`take_script_task_app_frame()` 总是在成功复制 sealed lease 后释放它，即使解码失败。UI task 不可缓存 lease
ID、`DisplayCommand*` 或 worker 侧 target 地址。

## 停止顺序

发生退出、watchdog、脚本 fatal 或预算失败时严格执行：

1. `ScriptTaskSupervisor::begin_teardown(session)`，关闭输入与新服务并丢弃未消费值包；
2. `ScriptTaskServiceBridge::begin_teardown(session)`，取消仍排队的 host job，保留已 in-flight 的记录；
3. 要求 script worker 在自己的 C-safe boundary 退出；不得在其他 task 析构 JerryScript 对象；
4. 终止 host app 并完成 host handle 回收；
5. `ScriptTaskServiceBridge::complete_teardown(session)`；
6. `ScriptTaskSupervisor::complete_teardown(session)`，回收 frame lease 与 release intents；
7. UI task 原子回到 launcher/recovery frame 后，才可复用 app/session 资源。

第 1 步成功后、到第 6 步成功前，`ScriptTaskSupervisor::begin()` 必须返回无效 session；port 不得
尝试绕过该门闩重启 worker。worker finalizer 在此期间仍可为 retiring session 投递 release intent，
但必须在第 6 步前由 supervisor drain 或由该步明确丢弃并计数。

平台无关 worker runtime 的对应调用是幂等的 `ScriptTaskWorkerRuntime::stop()`。port 必须在 worker
自己的 task boundary 内调用它；不能从 UI/supervisor task 直接析构 runtime，也不能把它当作
`begin_teardown`/`complete_teardown` 的替代品。

带 supervisor 的 `eval_with_supervisor()`、`process_one()` 与 `pump_callbacks()` 发现 fatal 后会自动尝试
`publish_fatal(supervisor)`；mailbox 背压时 worker 后续 tick 必须继续运行这些入口或显式重试，直到发布成功
或 supervisor 已失效。supervisor 从独立 fatal mailbox 解码 40-byte value packet 后再执行 session invalidation
和 bridge teardown。重复调用不得产生多个 fatal packet，也不得把 fatal packet 放入 frame 或 service mailbox。
初始化在任何 supervisor 入口之外失败时，port 必须显式调用 `publish_fatal()`；随后仍按本节的停止顺序处理。

## 当前证据与边界

桌面 `jellyframe_script_task_runtime_tests` 覆盖值协议、请求/完成/取消/拒绝、worker inbox 与
`service completion -> worker -> sealed frame -> UI` 闭环；scripting 桌面测试覆盖
`ScriptTaskWorkerRuntime` 的输入、timer、节点销毁安全和 sealed frame 发布。WS147 的 P3-1 至 P3-4
实机报告已补齐 launch/fail/recover、touch-to-frame、completion cancel、late completion、wrapper
teardown 和 mixed soak 证据，port 可以按本指南接入真实脚本 App。

这些证据只关闭当前 P3 合同，不把同线程 `JerryScriptRuntime` 变成可跨 task 共享的对象，也不证明
任意其他 SoC、panel、codec 或产品级脚本生态。新 port 仍必须复用 value-only packet、session generation、
sealed lease 和 C-safe worker boundary；不得把 `Node*`、JerryScript value、layer/display 指针或 host
handle 通过任务 mailbox 传递。
