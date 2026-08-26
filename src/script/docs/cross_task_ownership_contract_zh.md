# 脚本 App 跨任务所有权契约

> 最后更新：2026-08-15；适用版本：0.6.0-dev；状态：0.6 基础设施已落地

本契约定义 RTOS/多任务宿主如何运行一个真实 JerryScript App，而不把 DOM、JerryScript
资源或渲染对象跨任务传递。它是 P3 后续实现的前置条件；当前桌面同线程
`JerryScriptRuntime::bind_document(Node&)` 路径不自动满足本契约。

## 所有权模型

```text
system UI task
  owns: launcher, input sampling, presentation renderer, framebuffer, panel/DMA
  receives: sealed value-only AppFrame packets
  sends: value-only input packets

script worker task
  owns: one Jerry realm, private app DOM, JS wrappers/listeners/timers
  receives: input/service packets for its active session only
  emits: sealed AppFrame packets and typed service requests

app supervisor
  owns: session generation, worker lifetime, service scope, mailbox capacity,
        native-lease registry, recovery record and launcher fallback
```

每个运行实例必须使用 `ScriptAppSession`：`app_instance_id`、非零单调 `generation` 和
非零 `worker_epoch`。三个值共同标识一个 worker 生命周期。只比较 `app_instance_id`
不足以防止快速重启后的迟到包命中新 app。

禁止跨任何任务、queue、timer、callback、handle payload 或 fatal record 传递：

- `Node*`、`LayoutBox*`、`RenderObject*`、`LayerNode*`、`DisplayCommand*`；
- `jerry_value_t`、realm/context、wrapper、listener、timer callback、native pointer；
- `FrameBuffer*`、panel callback、DMA/GRAM 指针、GPIO/NVS/filesystem 句柄；
- 指向 task-local vector/string/arena 的地址。

跨任务数据只能是固定宽度 scalar、长度受限的字节副本，或由 supervisor 持有并带 generation
检查的不透明 lease ID。接收方必须在读取 payload 前验证完整 session；错误、过期或超预算包
只会被丢弃并计数，绝不尝试解引用发送方状态。
supervisor 会把 session transition 与 mailbox admission 放在同一短临界区，并按值返回 session snapshot；
teardown 不会与 worker 并发读写同一份 mutable generation 状态。
worker 侧每一次 `take_worker_packet(...)` 接收都必须显式传入自身的 `ScriptAppSession`；不保留
“读取当前 worker input”一类的便利 API，避免延迟退出的 worker 消费新生命周期的 packet。

## UI 帧交接

脚本 worker 可以持有真实的 app DOM，但 system UI task 不持有其 `Node`、layout tree 或
JerryScript wrapper。worker 在处理输入、timer 或 accepted completion 后，生成一个完整的、
不可变的 `AppFrame` 值快照：

- header：协议版本、`ScriptAppSession`、递增 `frame_sequence`、viewport、payload bytes；
- paint commands：只含 POD geometry/color/opacity/clip/transform、受限文本字节和资源 lease ID；
- 可选 hit regions：`target_key`、bounds、input flags 和 z-order；`target_key` 是 worker 私有 DOM
  映射的数值键，不是地址。首版以 raw normalized input 为权威路径，由 worker 用私有 DOM/layer tree
  命中；UI task 只可将已接受的 target region 用作加速提示；
- resource refs：只含 supervisor 分配的 app/session-scoped opaque ID，UI task 在接受帧前验证；
- 完整 replacement 语义：新 frame 取代前一个已接受 frame；第一版不发送 DOM patch、裸
  display-list 指针或跨任务 layer diff。

frame payload 必须先写入 supervisor-owned bounded lease，完成长度/预算检查后 seal，再以 ID
投递。UI task 只映射 sealed payload，并在 render 完成或丢弃时 release lease。worker 不能在
seal 后写入，UI task 不能把 frame 内地址回传给 worker。队满时可以丢弃尚未接受的旧 frame，
但不得丢弃已被 UI task 渲染中的 frame；必须记录 `frame-coalesced` 或 `frame-queue-full`。

这条路径允许触控驱动 DOM 重绘：UI task 命中当前已接受 frame 的 `target_key`，把归一化
pointer/key/wheel 值事件投递给 worker；worker 在自己的 DOM 内解析 key、派发 JS listener、
更新 dirty state 并发布下一完整 frame。UI task 不调用脚本 listener，不修改 worker DOM，
worker 不触碰 framebuffer 或 panel。

## 服务、取消与迟到完成

worker 提交的 service request 只包含 typed request value、`ScriptAppSession`、client token 和
可取消 request ID，并只进入独立的 worker-to-supervisor mailbox；completion 只进入 worker input
mailbox，frame 流量不与任一 service consumer 共用 queue。supervisor 是唯一能访问 `AppRuntimeHost`、request/completion queue 与
host handle table 的一方：

1. supervisor 在服务提交时绑定 session、request ID 和 native lease；
2. service worker 完成后先回 supervisor，而不是直接调用 JerryScript；
3. supervisor 仅把仍匹配 active session 且未取消的 completion 值投递给 script worker；
4. worker 在自己的 realm 中处理 completion，必要时发布新 frame；
5. 取消、worker exit 或 fatal 先让 supervisor 建立 tombstone、取消排队请求并释放/标记 lease；
   迟到 completion 只回收其 host handle 和统计 `completion-stale`/`completion-cancelled`。

native wrapper 只能保存 session-scoped opaque token，不能保存跨任务 `AppRuntimeHost*`、
service record 或 UI object 地址。wrapper finalizer 只能提交一个本地、可忽略的 release intent；
真正的 lease release 由 supervisor 幂等完成。这样 fatal teardown 或重复 finalizer 不会让已
销毁的 native resource 再被解引用。

## Fatal 与 teardown 顺序

Jerry fatal/VM halt 不得跨越 C++ 析构栈，也不得 `abort()`、reset system task 或重启 MCU。
port 必须在 script worker 内建立仅含 C/明确清理规则的 fatal boundary；fatal record 是纯值：
session、reason、有限 diagnostic code、heap/stack watermark 和最后安全 sequence。

supervisor 收到 fatal、watchdog、budget、worker exit 或主动终止后必须按以下顺序执行：

1. 关闭该 session 的输入接收与新 service 提交；
2. 使 session generation/epoch 失效，丢弃两个方向的旧 mailbox packet；
3. 取消排队 request，建立已 pop request 的 cancellation tombstone，冻结 native lease；
4. 等待 worker 退出到明确的 task boundary；不从其他任务析构 JerryScript object；
5. 回收 sealed/queued frame lease、host handle、字体/图片等 app 资源，并记录 teardown counters；
6. UI task 原子切回受信 launcher/recovery frame；
7. 只有所有 owner 都确认 release 后，才允许复用 heap、mailbox slot 或 session ID。

任何一步超时都只能终止该 app 并保持 launcher 可操作。不能将未确认的 worker 内存、native
wrapper 或 callback 重新绑定给下一个 app。
session 处于 retiring 状态时，supervisor 必须拒绝新 session；只有
`complete_teardown()` 已回收 retiring session 的 lease、tombstone 和排队 release intent 后，
`begin()` 才能接纳新的 worker。

## 预算与验收

协议必须分别限制 input、frame、service request、completion、lease bytes、native lease、timer
和诊断记录；所有 queue 都记录 posted/applied/coalesced/stale/cancelled/full/invalid 的计数。
最低验收应覆盖：

1. 触控 target -> JS listener -> DOM mutation -> 新 frame -> UI dirty present；
2. completion 成功、取消和 generation 改变后的迟到 completion；
3. 连续 frame 替换、队满和 UI render 中 frame 的 lease 生命周期；
4. watchdog、JS exception、fatal record 和 wrapper finalizer 同时触发时的 teardown；
5. 至少 30 次真实脚本 App launch/fail/recover，确认无跨 app frame/input/service、无悬垂
   native dereference、无系统 reset，且 launcher 始终可用。

在这些验证前，P3 mailbox preflight 只能证明 worker 隔离与定长值包的基础路径；它不能作为
真实脚本 App、DOM 重绘、服务或 fatal teardown 已完成的证据。

## 当前平台无关基础设施

当同时启用 `JELLYFRAME_BUILD_SCRIPTING=ON` 与 `JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME=ON` 时，独立
`jellyframe_script_task_runtime` target 会编译 `src/app_runtime/script_task_contract.*`，并实现和单测：
session generation/epoch 校验、固定槽
值 mailbox、session-scoped sealed frame lease、服务取消 tombstone/迟到 completion 分类、去重的
native release intent mailbox，以及不创建 task/VM 的两阶段 `ScriptTaskSupervisor` teardown。
`script_task_service_bridge.*` 将这些 token 映射到 `AppRuntimeHost` job，并把 completion value
序列化为固定 24-byte packet。该可选 target 不依赖 JerryScript、RTOS、DOM 或 renderer，不会自行启动 worker
或把 AppFrame 绘制到屏幕。`script_task_frame_codec.*` 现可把受限 `DisplayList`、viewport 与按绘制顺序
排列的不透明 input target key 编码为版本化 value frame；session 和 sequence 保留在外层 frame lease packet 中。
frame v3 额外为每条 command 携带有界的 1/1024 定点仿射矩阵，因此 worker 内的 `rotate()`、
`scale()` 不会在 frame 交给 UI task 时消失。frame v4 再为变换层自身的 overflow source surface 增加
可选且有界的 source-clip 引用；它与目标空间 clip chain 分离，使祖先裁剪、圆角和双线性采样边缘保持
与 compositor 等效。v1/v2 遇到带变换 command 会显式拒绝，v3 遇到 v4 source clip 也会显式拒绝，
不能再静默绘制未变换或裁剪不完整的结果。`make_script_task_app_frame()` 会先在 worker 内部 flatten 私有 `LayerNode`，再复制该
value frame。
`script_task_input_codec.*` 为 worker inbox 提供版本化 pointer、wheel、key 和受限 text value。
`script_task_input_dispatch.*` 只通过 worker 私有 `InputController` 消费这些 value。
`script_task_service_request_codec.*` 将 typed request 编码为固定 20-byte value，并将取消身份编码为
固定 12-byte value；supervisor 在 `ScriptTaskServiceBridge::submit_packet()` 或取消分流接触 host 前完成
解码和校验。
`ScriptTaskServiceBridge::pump_service_requests()` 是唯一的 request mailbox drain；它会给出各类
拒绝和取消计数，取消包经 `cancel_packet()` 分流，且不会消费 frame 或 worker inbox 数据。
已进入 wire queue 但被 host 拒绝的 request 会沿正常的有界 completion 路径返回终态 value，绝不静默丢失。
supervisor 还持有独立、按 session 隔离的 sealed service-payload lease registry。当前
`ScriptTaskServiceBridge` 已通过有界 `ScriptTaskServicePayloadWriter` 复制 host 结果，发布到该
registry 后才投递 completion packet version 3 中的 64-bit lease ID。port 必须提供仅 supervisor 可调用的
copy 与 provider-release callback；后者必须恰好一次释放 provider record 和 host-table entry，不能只把
generic handle-table release 当成资源回收。opaque host handle 永远不是 worker 可读取的数据；worker 用
`take_script_task_service_payload()` 复制并释放 lease。
`script_task_worker_inbox.*` 是 worker 内部的 input/completion value 接收器；绑定私有 realm 的
sink 不会拿到 host 或 UI 指针。

bridge 是 script session 期间唯一的 `AppRuntimeHost` completion consumer。规定的关闭顺序是：
`ScriptTaskSupervisor::begin_teardown`、bridge 取消 pending job、host 终止 App、bridge 回收记录，
最后 `ScriptTaskSupervisor::complete_teardown`。该顺序既不会把 stale value 投给 worker，也不会
遗漏 late completion 的 handle release。

`src/script/script_task_worker_runtime.*` 已提供第一片 worker 侧 DOM/display-list producer：它在一个
worker-owned 对象内持有解析后的 document、同线程 JerryScript binding、render/layout/layer tree 和
`InputController`，并在 value input 或 worker 内 timer/animation callback 修改 DOM 后只发布 sealed value
frame。Node 销毁观察器现在可组合注册，重建 layer tree 时会重新绑定交互状态；事件 dispatch 会报告目标已
销毁，使 pointer-up 不再继续执行依赖旧 Node 的默认动作。真实 RTOS task adapter 和
worker fatal boundary 仍是后续工作。当前 worker-local JS gateway 已提供受限的
`services.request(kind, callback, options)` 与 `services.cancel(requestId)`：请求元数据和取消身份只有标量。
可选 `inputHandle` 只表示 supervisor-owned 的请求输入资源，绝不是 completion result；完成结果始终留在
supervisor lease 后方，仅在复制并释放后以 `payloadBytes` 副本交给 JS callback。`services.cancel(requestId)` 只在取消 value packet 成功进入
mailbox 后移除 worker-local callback；supervisor bridge 再区分 queued/in-flight，迟到 completion 由
tombstone 消费并回收。provider policy、真实 RTOS task adapter 和 fatal boundary 仍是后续工作。port
不得用裸指针填补这些协议空缺。

`JerryScriptRuntime` 现在会把预算执行 callback 首个异常或 execution-budget failure 记录为纯值
`ScriptCallbackFailure`。`ScriptTaskWorkerRuntime` 在 input、timer/animation 或 service completion
dispatch 后消费它，生成有界 fatal record 并停止发布新 frame。这只是 worker-local fatal detection 第一片；
RTOS task 退出、supervisor recovery 与 launcher handoff 仍属于移植侧验收工作。
worker-local 对象现在提供幂等的 `ScriptTaskWorkerRuntime::stop()` 作为自身退出边界；它只能销毁
worker-owned realm、document 与 render state，不能使 session 失效，也不能释放 supervisor-owned
frame/service lease。后两者仍必须由 supervisor 按既定 teardown 顺序完成。
fatal 发布使用独立的固定 40-byte `FatalRecord` value packet 和 mailbox，与 frame/input/service 队列
分离。`publish_fatal()` 遇到有界 mailbox 背压可以重试；成功发布后不会重复投递。
