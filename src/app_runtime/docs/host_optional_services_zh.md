# 可选宿主服务接口契约

本文把图片/音频/轻量视频、网络数据请求、语义设备数据和安装式 app bundle 的实现边界具体化。它是
`host_abstraction_zh.md` 和 `embedded_hal_api_zh.md` 的补充：前者说明职责边界，后者列出
移植 checklist；本文描述移植侧可以照着实现的 V0 服务形状。

这些服务全部是可选的。`jellyframe_render_core` 不依赖 ESP-IDF、GMF、ADF、LVGL、socket、TLS、
文件系统或某个厂商 SDK。真实实现属于桌面壳、RTOS host、系统 shell 或开发板 port。

第一版平台无关辅助代码位于 `src/app_runtime/host_services.h` / `src/app_runtime/host_services.cpp`。
它提供有界 request queue、completion queue、host handle table 和基础状态类型；它不创建线程，
不执行 I/O，也不拥有任何平台资源。

`src/app_runtime/app_lifecycle.h` / `src/app_runtime/app_lifecycle.cpp` 提供第一版
app 实例生命周期 helper。它只负责生成 `app_instance_id`、记录 foreground/suspended 状态、
在 app 切换、退出或 crash recovery 时取消旧 request、丢弃旧 completion、释放旧 host handles，并在每帧消费
completion 时过滤 stale instance；同时会记录稳定 teardown reason。它不拥有 DOM、JS runtime、
framebuffer 或平台线程。

`src/app_runtime/app_host.h` / `src/app_runtime/app_host.cpp` 提供更高一层的
`AppRuntimeHost`：把 lifecycle controller、request queue、completion queue 和 host handle
table 放进同一个有界容器，并提供“当前 app 提交 job / 分配 handle / 每帧 pump completion”的固定入口。
它还提供 `terminate_current(reason)` / `crash_current()`，用于宿主在用户关闭、watchdog 中断、
app 加载失败或运行错误后执行同一套资源释放规则。
它仍然不执行网络、文件、解码或 flash I/O；真实工作由桌面壳、RTOS worker 或 port 层完成。

`src/app_runtime/app_service_worker.h` / `src/app_runtime/app_service_worker.cpp`
提供极薄的平台无关 worker pump。`pump_app_host_service_worker(...)` 不创建线程；它只按一个
`HostServiceJobKind` 弹出 request，调用宿主持有的 `AppHostServiceWorker`，归一化返回 completion
的身份字段，并推回 UI completion queue。这样真实 Wi-Fi、flash、codec 或 package-install worker
可以共享同一个边界，同时 DOM、JS、layout 和 framebuffer 仍只属于 UI task。

`src/app_runtime/app_services.h` / `src/app_runtime/app_services.cpp` 提供第一版平台无关 mock：
`NetworkFetchMock`、`ImageDecodeMock`、`AppPrivateKvStorageMock` 和 `AudioCommandMock`。
它们用于桌面验证和端到端契约测试，仍不访问真实网络、文件系统、codec、音频设备或 flash；真实产品
host 应以相同 request/completion/handle 语义替换其 worker 实现。

同一组文件还提供第一版 manifest/profile gate：`AppServiceManifestCapabilities`、
`AppServiceHostProfile` 和 `app_service_policies_for_app(...)`。manifest 中的
`network.fetch`、`storage.kv` 或 `media.audio.mp3` 只表示 app 请求能力；只有被选中的
host/profile 同时允许该服务，并提供有界预算时，最终 runtime policy 才会启用。这样 JS binding 和
worker 实现都不需要各自散落权限判断。

`src/app_runtime/app_device_services.h` / `src/app_runtime/app_device_services.cpp` 提供第一版语义设备
服务 mock：`AppSensorSampleMock` 和 `AppLocationSnapshotMock`。它们覆盖加速度计、陀螺仪、
心率、环境光和定位快照的 request/completion/handle 形状。真实产品仍由宿主决定这些数据来自哪颗传感器、
哪条总线、哪个 RTOS task 或哪个手机伴侣服务；App 只能请求文档化能力，不能拿到裸 GPIO/I2C/SPI/BLE/GPS
句柄。

`src/app_runtime/app_capability_broker.h` 提供更通用的 capability broker，用于标准能力和产品私有能力名。
它会把 app 请求分类为 `granted`、`granted-product-specific`、`unsupported-by-host` 或
`unknown-capability`。已知名称覆盖当前服务和未来语义化设备通道，例如 `sensor.accelerometer`、
`sensor.gyroscope`、`sensor.heart-rate`、`sensor.ambient-light`、`location.position`、
`connectivity.status`、`connectivity.companion`、`media.microphone`、`media.camera` 和
`media.video.input`。manifest 可以声明产品私有名称，但只有 host/profile 明确列出同名能力时才会
被授予。它们仍必须通过有界 host service 或宿主提供的 JS binding 暴露；app 不会拿到裸
GPIO/I2C/SPI/radio 句柄。

## 总体模型

JellyFrame 只有一个 UI owner：

- UI/main task 拥有 DOM、JerryScript、style、layout、layer、display list 和 framebuffer。
- worker task 负责 decode、network、bundle install、文件 I/O 等慢任务。
- worker task 不能直接修改 DOM，不能执行 JS，不能调用 layout/render，不能写 framebuffer。
- worker 完成后向 UI queue 投递小型 completion event。
- UI/main task 在帧边界按预算消费 completion event，再派发 DOM/JS event 或标记 dirty。

推荐 port 侧保留三个队列：

```text
request queue    UI/main -> worker
completion queue worker  -> UI/main
resource cache   host-owned surfaces/buffers/audio handles/bundles
```

`HostAsyncCapabilities::max_in_flight_jobs` 限制 request queue 中未完成 job 数量。
`HostAsyncCapabilities::max_completion_events_per_frame` 限制每帧最多消费的 completion event 数量。

当前 `app_runtime` helper 对应：

- `AppLifecycleController`：管理当前 active app instance、显式 suspend/resume，以及
  launch/exit/crash 时的 request/completion/handle teardown。
- `AppRuntimeHost`：组合 lifecycle、request/completion queue 与 handle table，作为桌面壳和
  MCU host 接入可选服务的推荐状态容器。
- `pump_app_host_service_worker(...)`：可选的真实 host worker helper，用固定预算处理一个 service kind，
  并把归一化 completion 投回 UI queue。
- `pump_app_host_service_workers(...)`：可选的静态 service worker 表 helper。它按每个非空 slot 自己的
  固定 request budget 泵送，并在 UI completion queue 已满时停止，避免继续弹出工作。
- `HostServiceRequestQueue`：有界 request FIFO，支持 priority 选择、pending job 取消和按
  `app_instance_id` 批量取消。只负责单一 service kind 的 worker 应使用按 kind 过滤的 pop，
  避免 network、storage、image、media job 彼此误消费。
- `HostServiceCompletionQueue`：有界 completion FIFO，支持每帧按上限 pop，并可丢弃旧
  `app_instance_id` 的 completion。
- `HostHandleTable`：有界 host handle 表，使用 generation 防止释放后的旧 handle 误命中新资源，
  并统计 active count 与 used bytes。
- `AppSystemEventQueue`：有界宿主注入系统状态事件，事件绑定 active `app_instance_id`，并在帧边界消费。
- `AppSensorSampleMock` / `AppLocationSnapshotMock`：语义设备数据服务的桌面/测试 mock。它们只保存小型
  sample/snapshot record，并通过 host handle 回到 UI task。

## 通用 Job 结构

推荐使用单调递增的 32-bit job id。id 只需要在一次开机周期内唯一。

```cpp
enum class HostServiceJobKind {
    ImageDecode,
    AudioCommand,
    VideoFrameDecode,
    NetworkFetch,
    StorageKv,
    SensorSample,
    LocationSnapshot,
    BundleInstall,
    BundleRemove,
};

enum class HostServiceStatus {
    Completed,
    Failed,
    Cancelled,
    Unsupported,
    BudgetExceeded,
    Timeout,
};

struct HostServiceCompletion {
    uint32_t job_id;
    HostServiceJobKind kind;
    HostServiceStatus status;
    uint32_t app_instance_id;
    uint32_t handle;
    uint32_t error_code;
    uint32_t byte_count;
};
```

`app_instance_id` 用于 app 切换、页面销毁或系统休眠时隔离旧 job。UI 收到 completion 时，如果
`app_instance_id` 已不是当前实例，应释放相关 host handle，并忽略 DOM/JS 回调。
`AppLifecycleController::pump_completions()` 提供这个过滤策略的参考实现：当前实例的 completion
进入业务处理列表；旧实例 completion 会被消费并释放其 handle，避免旧网络响应、旧图片 surface 或旧音频状态
回调到新 app。

`handle` 是宿主资源层的短句柄，不是裸指针。句柄可以指向 decoded surface、audio stream、
network response buffer 或 bundle staging record。句柄生命周期由宿主控制。

Win32 参考壳的 A4 行为：

- package loader、JerryScript runtime、timer pump、输入派发和 completion pump 都绑定当前 active
  `app_instance_id`。
- 旧实例或非 foreground 实例不会接收输入，也不会泵动脚本 timer。
- app rebuild/load 失败时，壳释放资源并回到 system shell。新代码优先使用
  `terminate_current(AppTeardownReason::LoadFailure)` 保留精确原因；`crash_current()` 仍保留为
  runtime error 的便捷入口。

suspend/resume 策略：

- `suspend_current()` 是状态切换，不是 teardown。它用于息屏、后台和低功耗等后续可能恢复的状态。
- `AppFramePolicy` 是第一版代码化策略入口：foreground + screen-on 正常消费输入、timer、rAF 和 present；
  low-power + screen-on 保留输入/timer/present 但关闭 animation；screen-off 或 suspended 会暂停前台输入、
  timer、rAF 和 present。
- suspended 期间，宿主应停止 foreground 输入、脚本 timer、`requestAnimationFrame` callback 和非必要
  CSS animation sampling；实现方式可以是把对应 frame-loop/animation budget 置 0，或直接不泵这些队列。
- 慢速 host job 是否继续由产品策略决定。completion 必须继续带原始 `app_instance_id`；宿主可以把业务投递缓存到
  resume 后，但旧实例 completion 绝不能修改后续新 app。
- resume 后，宿主应在第一帧可交互前调度 repaint；如果产品需要确定性状态，可重新注入 network/visibility
  这类小型状态快照。
- `exit_current()` / `terminate_current(reason)` / `crash_current()` 仍是 teardown 边界：取消旧 request、
  丢弃 completion、释放 handle，并清理 app-local resource。稳定 reason 名称包括
  `normal-exit`、`app-switch`、`user-kill`、`runtime-error`、`script-watchdog`、
  `budget-exceeded`、`load-failure` 和 `system-policy`；其中 `runtime-error`、
  `script-watchdog`、`budget-exceeded` 和 `load-failure` 会被归为 crash-like。

## Worker Pump Helper

宿主可以用桌面线程、RTOS task、协作式循环或测试 harness 跑 worker。JellyFrame 只要求
request/completion 边界一致：

```cpp
class AppHostServiceWorker {
public:
    virtual HostServiceCompletion process(const HostServiceRequest& request) = 0;
};

AppHostServiceWorkerPumpResult result =
    pump_app_host_service_worker(host,
                                 AppHostServiceWorkerPumpOptions{
                                     HostServiceJobKind::NetworkFetch,
                                     1,
                                 },
                                 network_worker);
```

规则：

- 每个 worker 只 pump 一个 service kind，避免 image、network、storage 和 audio job 互相消费。
- `max_requests` 保持很小。MCU 目标通常每个 worker tick 只处理 `1` 个，或使用产品固定预算。
- helper 会用原 request 的 `job_id`、`kind` 和 `app_instance_id` 覆盖 completion 身份；
  stale-instance 保护仍由 UI completion pump 负责。
- 如果 UI completion queue 已满，helper 会在弹出 request 前返回 `completion_queue_full`。
  宿主应稍后重试，而不是忙等。
- worker 实现不得调用 DOM、JS、style、layout、render 或 framebuffer API。它只返回小型 completion
  和宿主持有的 handle。

推荐 port 结构：

```text
UI/main task:
  1. 处理输入/timer/system event。
  2. pump_frame_completions(...)，只把 accepted completion 派发给当前 app。
  3. 根据 dirty 标记决定是否 layout/render/present。

Network worker:
  pump_app_host_service_worker(host, { NetworkFetch, 1 }, network_worker)

Storage worker:
  pump_app_host_service_worker(host, { StorageKv, 1 }, storage_worker)

Audio worker:
  pump_app_host_service_worker(host, { AudioCommand, 1 }, audio_worker)

Sensor worker:
  pump_app_host_service_worker(host, { SensorSample, 1 }, sensor_worker)

Location worker:
  pump_app_host_service_worker(host, { LocationSnapshot, 1 }, location_worker)
```

对协作式 MCU loop，也可以不用动态分配地表达同一策略：

```cpp
AppHostServiceWorkerSlot workers[] = {
    { HostServiceJobKind::NetworkFetch, 1, &network_worker },
    { HostServiceJobKind::StorageKv, 1, &storage_worker },
    { HostServiceJobKind::AudioCommand, 1, &audio_worker },
};

AppHostServiceWorkerGroupPumpResult pumped =
    pump_app_host_service_workers(host, workers, 3);
```

group helper 只是调度便利层。它不创建线程，不无限重试，也不会在 completion queue 已满时继续运行
service 代码。

MCU port 不必真的创建三个线程；可以是 RTOS task、事件循环分支，甚至一个协作式后台循环。关键是：

- 慢服务不要在 UI/main task 中同步执行。
- completion queue 满时停止提交 completion，等待下一帧 UI task 消费后再继续。
- request queue 满、completion queue 满、timeout、capability denied 都应进入 port 日志或桌面 diagnostics。
- Win32 壳 frame capture 会输出 `host_completion_*`、`system_event_*`、`frame_policy_*` 和
  `service_activity` 汇总，可作为 port 日志字段的参考。

## 图片解码服务

用途：

- 未来 `<img>`。
- 未来 CSS `background-image` 的本地 package 图片。
- 桌面预览或 app manager 的图标解码。

输入应来自本地 package、已安装 bundle 或系统资源表。当前阶段不允许远程图片直接进入页面 loader。

当前 V0 helper：

- `ImageDecodePolicy` 定义 enable gate、URL 长度、最大宽高、decoded bytes 和 pending decode 上限。
- `ImageDecodeMock` 提供桌面/测试用 raw surface fixture，走 `HostServiceJobKind::ImageDecode`
  request/completion。
- `AppImageSurfaceCache` 把 URL 变成有界 decode request，completion 后记录 ready `Surface`
  handle，并在页面/实例切换时释放 surface。它也可按 ready surface 数量和 decoded bytes
  预算回收未被当前 display list 引用的 LRU surface。
- 直接调用 `AppImageSurfaceCache::handle_completion(...)` 时会拒绝旧 app instance 的 completion。
  正常 `AppRuntimeHost` completion pump 本来已经会过滤 stale completion 并释放返回的 handle；
  这里额外兜底是为了避免宿主/调试代码误把旧 surface 挂到新 app 上。
- `evict_unreferenced_with_result(...)` 会同时报告释放的 surface 数和预算清理中丢弃的 stale cache
  entry 数。stale drop 非零通常说明某个 surface handle 在 cache 生命周期之外被释放，宿主应记录日志。
- `classify_app_image_failure(...)` / `app_image_failure_detail(...)` 把 request 拒绝和
  completion 失败归类为 `capability-denied`、`resource-not-found`、
  `decode-budget-exceeded`、`surface-budget-exceeded`、`pending-budget` 等稳定原因。
  桌面工具和未来串口/包校验 diagnostics 应消费这个分类，而不是只显示模糊的 `failed`。
- `AppImageSurfaceCache::diagnostic_detail_for_url(...)` 暴露稳定调试字符串，包含
  `src`、`state`、`reason`、`submit`，以及可选的 `host`、`error`、`job`、`handle`、
  `bytes` 字段。工具应在 request 拒绝或 decode completion 后使用它，把异步失败定位回
  触发问题的 cache entry。
- 成功 completion 返回 `HostServiceHandleKind::Surface` handle；`AppDecodedSurfaceRecord`
  保存 width、height、stride、pixel format 和可选 raw pixels。
- `release_surface(...)` 必须由 UI/main task 在 surface 不再需要时调用，释放 record 和 host handle。
- 如果 app switch/crash 已由 `AppRuntimeHost` 释放 surface handle，mock 可用
  `collect_released_surfaces(...)` 清理 stale surface record；真实服务也应在生命周期 hook 中执行同类清理。
- render core 已提供 `ImageHandleResolver`、image display command 和 `ImagePainter`。宿主可以在
  layer tree 构建时把 `<img src>` 映射到 decoded surface handle，在 paint 阶段用 painter 绘制。
- Win32 browser debug 壳已接入 `/debug/icon.raw` 和 `/debug/photo.raw` raw RGB565 fixture：
  第一次 paint 会提交 decode，completion 回到 UI/main task 后标记 `DomDirtyPaint` 并重绘。
  app 页面应使用 package-local 标准路径。

未来宿主 request 可以映射为：

```cpp
struct HostImageDecodeRequest {
    uint32_t app_instance_id;
    const char* resource_path;
    HostPixelFormat output_format; // usually Rgb565 or Rgba8888
    uint16_t max_width;
    uint16_t max_height;
    uint32_t max_decoded_bytes;
};
```

result handle 指向：

```cpp
struct HostDecodedSurface {
    uint16_t width;
    uint16_t height;
    uint16_t stride_pixels;
    HostPixelFormat pixel_format;
    const void* pixels;
};
```

规则：

- 大图应在打包期缩放或拒绝。运行时不要为超大图片尝试“赌一把”。
- 解码输出应优先匹配屏幕或 compositor 需要的格式。ESP32-S3 推荐 RGB565。
- decoded surface 由宿主 cache 持有；UI 只引用 handle。
- cache 满时可以 LRU 回收未被当前 display list 引用的 surface；如果当前画面引用了所有
  ready surface，可以暂时超过预算，等待后续 frame 回收。
- 如果解码失败，页面保留占位盒并报告 diagnostics。
- Win32 debug 壳已接入 `.jfapp`/源码包无压缩 24/32-bit BMP loader，用于验证包内图片资源到
  surface handle/display list/repaint 的完整路径。
- Win32 debug 壳会报告图片 request 拒绝和 completion 失败，包括触发的 `src`、稳定失败原因、
  submit/host 状态和 host error code。
- render core 会把 `object-fit` 子集传给宿主 painter；Win32 debug painter 已支持
  `fill`、`contain`、`cover`、`none`、`scale-down`，并支持关键词/百分比一二值
  `object-position`。复杂四值和长度偏移延后。
- PNG/JPEG/WebP 和产品级 MCU codec 尚未接入。

## 音频播放服务

用途：

- 提示音、闹钟、按钮反馈、短语音。
- 未来 JS API 或系统 shell 控制的 app audio。

音频服务不应把 PCM 交给核心。宿主拥有 codec、I2S、GMF/ADF pipeline 和音频 task。

推荐命令：

```cpp
enum class HostAudioCommandKind {
    Open,
    Play,
    Pause,
    Stop,
    Close,
    SetVolume,
};

struct HostAudioCommandRequest {
    uint32_t app_instance_id;
    HostAudioCommandKind command;
    uint32_t audio_handle;
    const char* resource_path;
    uint8_t volume;
};
```

completion event：

- `Open` 成功返回 `audio_handle`。
- `Play`、`Pause`、`Stop` 返回状态。
- `Close` 释放 `AudioStream` handle。
- `SetVolume` 会把音量限制在 0-100。
- 播放自然结束时投递 `Completed`。
- 错误时投递 `Failed` 和 host error code。

规则：

- 当前平台无关代码提供 `AudioCommandMock`，用于验证 request/completion/handle 生命周期；它不真正播放音频。
- Win32 壳提供 `--audio-smoke`，可用本地文件或 `--app` 包内 `/audio/...` 资源验证桌面 host adapter。
  这仍是宿主验证路径，不改变核心，也不表示 MCU 端内置 codec。
- `HostMediaCapabilities::max_audio_streams` 通常在手表上设为 1。
- app 切换或锁屏策略触发时，系统 shell 可以停止或暂停 app audio。
- app teardown 会释放 host handles；具体服务实现也应在自己的生命周期 hook 中清掉 stale stream record，
  mock 通过 `collect_released_streams(...)` 覆盖这一路径。
- `classify_app_audio_failure(...)` / `app_audio_failure_detail(...)` 会把 request 拒绝和
  completion 失败归类为稳定诊断：`capability-denied`、`invalid-source`、`source-not-found`、
  `invalid-handle`、`stream-budget-exceeded`、`command-timeout`、`command-cancelled`
  等原因。
- worker/audio task 不得调用 JS；只投递事件，由 UI task 派发 `ended`/`error` 等回调。

## 语义设备数据服务

用途：

- 运动、健康、表盘和天气类 app 读取加速度计、陀螺仪、心率、环境光或定位快照。
- 产品私有传感器通过 capability broker 暴露语义名称，而不是把硬件总线暴露给第三方 app。
- 后台 app 采样策略由 `AppFramePolicy` / `AppBackgroundServicePolicy` 控制，宿主可以在低功耗、
  息屏或 suspended 状态降低频率或停止采样。

当前平台无关代码提供：

- `AppSensorSampleMock`：通过 `HostServiceJobKind::SensorSample` 请求一个小型传感器样本。
  `AppSensorKind` 覆盖 `accelerometer`、`gyroscope`、`heart-rate` 和 `ambient-light`。
- `AppLocationSnapshotMock`：通过 `HostServiceJobKind::LocationSnapshot` 请求一个定位快照。
- `HostServiceHandleKind::SensorSample` / `LocationSnapshot`：结果由短句柄引用，App/JS binding
  在 UI task 消费 completion 后复制出需要的字段，再释放句柄。
- `app_sensor_sample_policy_from_service_policies(...)` 和
  `app_location_snapshot_policy_from_service_policies(...)`：把 manifest + host/profile gate
  合并后的策略转换成具体服务 policy。
- `classify_app_device_failure(...)`：把 request 拒绝和 completion 失败归类为稳定 diagnostics：
  `capability-denied`、`sample-unavailable`、`record-budget-exceeded`、`handle-budget-exceeded`、
  `request-timeout`、`request-cancelled` 等。

规则：

- App 必须先在 manifest 声明 `sensor.accelerometer`、`sensor.gyroscope`、`sensor.heart-rate`、
  `sensor.ambient-light` 或 `location.position`，并且 host/profile 同时允许，服务才会启用。
- sample/snapshot 是离散数据快照，不是无限制高频 stream。真实宿主可以把连续硬件采样降采样、
  去抖或缓存成最近一次快照，再按 request/completion 边界投递。
- worker 不得调用 DOM/JS/layout/framebuffer。传感器中断、GPS/BLE/Wi-Fi 定位、手机伴侣数据等都必须
  归一化为小型 completion 或 host handle。
- `max_sensor_sample_records` 和 `max_location_snapshot_records` 限制未释放 record 数量；host handle
  byte budget 仍是第二道保护。
- app 切换、退出或 crash recovery 会释放 host handles；真实服务也应像 mock 的
  `collect_released_samples(...)` / `collect_released_snapshots(...)` 一样清掉 stale record。
- 低功耗/息屏策略由宿主产品决定。默认建议停止后台传感器采样；确实需要计步、心率或定位的产品，应在
  manifest 意图、用户授权和系统电源策略三者同时允许后才继续。

## 后台服务活动策略

`jellyframe.app.json` 中的 `backgroundServices` 是意图声明，不是权限授予：

```json
{
  "backgroundServices": {
    "network": { "whileSuspended": true, "whileScreenOff": false },
    "audio": { "whileSuspended": true, "whileScreenOff": true },
    "sensors": { "whileSuspended": false, "whileScreenOff": false, "inLowPower": false }
  }
}
```

宿主会把这个意图与产品策略、用户设置和系统状态合并，然后写入
`AppBackgroundServicePolicy`。平台无关 helper `app_service_activity_policy_for(...)`
会返回：

- `network_fetch`：网络 service worker 是否还能接受新的 app fetch；
- `audio_playback`：app audio 是否可以继续播放；
- `sensor_sampling`：传感器采样是否可以继续；
- `should_pause_audio`：shell 是否应暂停/停止当前音频流；
- `should_throttle_sensors`：传感器采样是否应降频或停止。

默认策略刻意保守：前台 app 可以使用已批准服务，但 suspended app 和 screen-off 状态会暂停后台工作，
除非宿主明确允许。低功耗模式默认节流传感器，除非宿主设置 `sensors_in_low_power`。completion
仍必须带原始 `app_instance_id`；后台工作可能在 app 不再 active 后完成，但 stale completion 不得修改新实例。

## 轻量视频/MJPEG/H.264 实验服务

当前只建议作为实验性 frame provider，不承诺 `<video>`，也不把视频作为普通页面 layout 的必需能力。

推荐范围：

- 低分辨率 MJPEG。
- 目标 profile 明确开启时的低分辨率 H.264 baseline frame decode。
- 帧输出 RGB565。
- 固定或较低 fps，例如 10-15fps。
- 不与主 UI 帧率绑定；掉帧优先于阻塞 UI。

推荐 request：

```cpp
enum class HostVideoCodecKind {
    Mjpeg,
    H264Baseline,
};

struct HostVideoFrameRequest {
    uint32_t app_instance_id;
    HostVideoCodecKind codec;
    const char* resource_path;
    HostPixelFormat output_format; // usually Rgb565
    uint16_t max_width;
    uint16_t max_height;
    uint8_t max_fps;
    uint32_t max_frame_bytes;
    uint32_t timeout_ms;
};
```

推荐 result handle 指向 latest-frame surface：

```cpp
struct HostVideoFrame {
    uint16_t width;
    uint16_t height;
    uint16_t stride_pixels;
    HostPixelFormat pixel_format;
    const void* pixels;
    uint32_t pts_ms;
    bool dropped_previous;
};
```

规则：

- H.264 不进入 ESP32-S3 默认 profile。2026-06-20 复测证明它在 QEMU + Octal PSRAM 下可跑通，
  但 320x192 baseline 样本仍低于实时，只能作为实验 profile。
- video frame buffer 由宿主持有，UI 只拿最新 frame handle。
- 如果 `supports_h264` 为 false，打包/安装工具应拒绝声明 H.264 的 app，或给出降级诊断。
- 如果解码积压，丢弃旧帧，不追赶历史帧。
- 若 dirty repaint 无法跟上，暂停视频或降低 fps。
- H.264 decoder、YUV->RGB565 转换、PSRAM/cache 细节和任务调度都属于宿主；核心只接收 frame
  handle/completion，不接触码流或大像素 buffer。

## 网络数据服务

网络只用于 runtime data API，不用于页面资源 loader。

当前 V0 mock：`NetworkFetchMock` 用固定 fixture 模拟 `network.fetch`，通过
`HostServiceJobKind::NetworkFetch` 提交 request，completion 返回 `FetchResponse` handle。
它会检查 capability、URL 长度、响应 byte budget 和 request queue 上限。响应 body 由 mock
持有，UI/main task 只能通过 handle 查询并显式释放。

`AppXmlHttpRequest` 会把这个服务映射成平台无关的异步 XHR V0 状态机：`open("GET", url, true)`、
`send()`、`abort()`、`readyState`、`status`、`responseText`、`responseURL`、content type，以及
JerryScript binding 使用的标准事件序列。

能力 gate：

```cpp
AppServiceHostProfile profile =
    app_service_host_profile_from_capabilities(device_capabilities, storage_policy);
AppServicePolicies policies =
    app_service_policies_for_app(manifest_capabilities, profile);
NetworkFetchMock network(policies.network);
```

只有 app manifest 请求了 `network.fetch`，并且 host profile 允许有界 fetch job 时，
`policies.network.enabled` 才为 true。

推荐 request：

```cpp
enum class HostFetchMethod {
    Get,
    Post,
};

struct HostFetchRequest {
    uint32_t app_instance_id;
    HostFetchMethod method;
    const char* url;
    const uint8_t* body;
    uint32_t body_size;
    uint32_t max_response_bytes;
    uint32_t timeout_ms;
};
```

推荐 result handle 指向 bounded response buffer：

```cpp
struct HostFetchResponse {
    uint16_t status_code;
    const char* content_type;
    const uint8_t* bytes;
    uint32_t byte_count;
};
```

规则：

- 默认 `network.allows_remote_page_resources = false`。
- 不支持远程 HTML/CSS/script/image 参与页面加载。
- 目标 profile 可以限制域名、scheme、并发数、响应大小和超时。
- TLS 证书、DNS、重试、缓存策略都属于宿主，不属于核心。
- response buffer 不能直接暴露为可长期持有的大 JS 对象；JS binding 应复制小数据或提供有界读取。
- 如果 app switch/crash 已由 `AppRuntimeHost` 释放 response handle，mock 可用
  `collect_released_responses(...)` 清理 stale response record；真实服务应在自己的生命周期 hook 中做同类清理。
- `classify_app_network_failure(...)` / `app_network_failure_detail(...)` 会把 request 拒绝和
  completion 失败归类为稳定 diagnostics：`capability-denied`、`invalid-url`、
  `resource-not-found`、`offline`、`response-budget-exceeded`、
  `response-handle-budget-exceeded`、`request-timeout`、`request-cancelled` 等原因。
- XHR 仍保持接近 Web 且很小的表面：app JavaScript 只看到 `error`、`timeout` 或 `abort`；
  更细 reason 用于 CLI、Win32 diagnostics、串口日志和宿主验收。

## App 私有 KV Storage 服务

存储只用于 app 私有小数据，不提供浏览器级持久同步 `localStorage`、cookie、IndexedDB、Cache API
或通用文件系统。目标是支持设置、token、小型 JSON 状态、离线缓存索引等常见嵌入式 app 需求。

当前 V0 mock：`AppPrivateKvStorageMock` 使用 app id 隔离命名空间，通过
`HostServiceJobKind::StorageKv` 异步完成 `get/set/remove/clear`。`get` 成功时返回
`StorageValue` handle；`set`、`remove` 和 `clear` 只返回状态。mock 会检查 key 长度、单 value
大小、每 app item 数和总 byte budget。
它现在和 network/image mock 一样提供 `complete_request(...)`：真实 host worker 可以先通过
`pump_app_host_service_worker(...)` pop 出 `StorageKv` request，再生成 completion；`complete_next(...)`
仍保留为不使用通用 worker pump 的测试或协作式循环的一步式 helper。
如果 app switch/crash 已由 `AppRuntimeHost` 释放 `StorageValue` handle，mock 可用
`collect_released_values(...)` 清理 stale value record；真实 storage worker 也应在生命周期 hook 中执行同类清理。

能力 gate：只有 app manifest 请求了 `storage.kv`，并且 host profile 提供启用状态的
`AppPrivateKvPolicy` 时，`policies.storage.enabled` 才为 true。key/value/item/byte 预算会被复制到
最终 mock 或产品 worker 使用的具体 storage policy 中。

`AppLocalStorageShadow` 是标准 `localStorage` V0 子集的小型内存 helper。它复用
`AppPrivateKvPolicy` 限制，用紧凑顺序表保存字符串 key/value，不执行任何宿主 I/O。当前
JerryScript binding 只有在宿主绑定这个非阻塞 shadow 时才暴露 `localStorage`；持久化、恢复和
flush/drop 策略仍属于宿主异步 storage 工作。

存储 diagnostics：

- `classify_app_storage_failure(...)` / `app_storage_failure_detail(...)` 会把异步 storage
  rejection 和 completion failure 归类为稳定原因：`capability-denied`、`invalid-key`、
  `value-budget`、`quota-exceeded`、`not-found`、`handle-budget-exceeded`、
  `operation-timeout`、`operation-cancelled`。
- `classify_app_local_storage_failure(...)` 会把内存型 `AppLocalStorageShadow` 的状态映射到同一组
  reason。
- mock completion 使用少量约定 error code，仅用于 diagnostics：`404` 表示 key/resource 缺失，
  `413` 表示单 payload/value 超预算，`507` 表示 quota 或 handle budget 耗尽。

推荐命名空间：

```text
app_id -> key -> bounded bytes
```

推荐命令：

```cpp
enum class HostStorageCommandKind {
    Get,
    Set,
    Remove,
    Clear,
    Keys,
};

struct HostStorageRequest {
    uint32_t app_instance_id;
    HostStorageCommandKind command;
    const char* app_id;
    const char* key;
    const uint8_t* value;
    uint32_t value_size;
    uint32_t max_response_bytes;
    uint32_t timeout_ms;
};
```

推荐 result handle 指向宿主持有的短生命周期 response：

```cpp
struct HostStorageResponse {
    const uint8_t* bytes;
    uint32_t byte_count;
};
```

规则：

- 所有操作异步完成，通过 completion event 回到 UI/main task。
- 每个 app 有独立命名空间，不能枚举或读取其他 app 数据。
- key 长度、单 value 大小、总 byte 配额、item 数量和每帧 completion 数必须有上限。
- `Set` 应尽量使用宿主侧原子写或 journal，避免掉电后破坏已有 key。
- app 删除时由系统策略决定删除私有 storage 或保留用户数据；开发 mock 应提供显式清理。
- JS API 暴露前先完成配额、错误码、崩溃恢复和测试。面向用户的 storage 应只在宿主能通过
  app 私有内存 shadow 保证非阻塞时暴露极小 `localStorage` 子集；否则继续不暴露 storage。

生命周期策略：

- `AppStorageLifecyclePolicy` 和 `app_storage_lifecycle_decision_for(...)` 描述 pending writes
  与持久化 app data 的平台无关策略边界。
- 默认行为刻意保守：正常 app exit 会 flush pending writes；crash 和 memory pressure 会 drop pending
  writes；uninstall 会 drop pending work 并删除持久数据；update replacement 会 flush pending writes
  并保留数据。
- `drop_pending_writes` 优先于 `flush_pending_writes`。如果产品策略要求丢弃崩溃实例的数据，宿主不应再尝试
  flush 来自崩溃 JS/DOM 实例的 pending 数据。
- `AppPrivateKvStorageMock::drop_pending_app_instance(...)` 和 `drop_pending_app(...)` 提供桌面验收路径。
  真实 port 应实现等价的 worker queue cancellation 或 journal discard。
- `AppPrivateKvStorageMock::flush_pending(...)` 和
  `apply_app_storage_lifecycle_decision(...)` 提供第一版平台无关参考实现：宿主可按 frame/事件预算分批
  flush pending writes，并获得 flushed、dropped、deleted、remaining 等统计。真实 port 可以复用同样的
  决策结构，但具体 flash/NVS/filesystem 写入仍必须在 host worker 中完成。
- flush work 必须是有界 host job。不要让 UI/main task 阻塞在 flash、NVS 或 filesystem 写入上。

## Bundle 安装服务

安装式第三方 app 应由系统 shell/app manager 管理。

推荐安装流程：

1. 下载或接收 `.jfapp` 到 staging 区。
2. 校验 header、manifest summary、目标设备、版本、预算、hash/签名。
3. 校验资源索引排序、offset/size、路径规范化和总大小。
4. 写入 bundle store。
5. 原子提交 installed-app registry。
6. 投递 completion event，启动器刷新 app 列表。

推荐 registry 记录：

```text
app_id
version_name / version_code
display_name
icon_resource_path
permissions / capabilities
bundle_offset
bundle_size
validation_state
```

规则：

- 活动 app 的 JS 不能直接安装并挂载新 bundle。
- 安装失败只能丢弃 staging，不能破坏已提交 registry。
- 升级应先完整写入新 bundle，再原子切换 registry。
- 删除活动 app 时应先切回系统 shell。

## System Data Events

System data events 用于把小型、经宿主允许的系统状态快照提供给 app，同时不把硬件 API 放进核心。
第一版平台无关 helper 位于 `src/app_runtime/system_events.h`，类型为 `AppSystemEventQueue`。

支持的事件类型：

```cpp
enum class AppSystemEventKind {
    TimeChanged,
    TimezoneChanged,
    NetworkStatusChanged,
    BatteryChanged,
    ScreenStateChanged,
    LowPowerModeChanged,
};
```

状态快照刻意保持很小、可直接复制：

```cpp
struct AppSystemStateSnapshot {
    uint64_t unix_time_ms;
    int16_t timezone_offset_minutes;
    uint8_t battery_percent;
    bool charging;
    bool network_online;
    bool screen_on;
    bool low_power_mode;
};
```

注入状态：

```cpp
enum class AppSystemEventPushStatus {
    Accepted,
    EmptyInstance,
    QueueFull,
};
```

规则：

- 宿主使用 `push_current(...)` 为当前 app instance 注入事件；需要诊断原因时使用
  `try_push_current(...)`，它会区分 `empty-instance` 和 `queue-full`。
- UI/main task 在帧边界通过 `pump_current(...)` 消费事件。
- `max_events_per_frame` 限制每帧事件处理量。
- 旧 app instance 的事件会被消费并丢弃。
- 队列本身不调用 JS、不修改 DOM、不读取 RTC/network/battery 硬件，也不直接触发 layout；后续 binding
  决定 accepted event 如何变成 app callback。
- 当前 JerryScript binding 将网络状态映射到 `navigator.onLine` 和 `window` 的
  `online`/`offline` 事件子集，将 visibility 映射到 `document.hidden`、
  `document.visibilityState` 和 `document` 的 `visibilitychange`。battery JavaScript API
  不进入 V0。
- Win32 debug 壳可以通过 `Ctrl+F6`/`Ctrl+F7`/`Ctrl+F8` 和 frame script
  （`network-online/offline`、`screen-visible/hidden`、`low-power-on/off`）注入 fake event，
  方便 app 调试；注入失败会报告 `system-event-rejected` diagnostics。硬件 port 应从自己的
  host state provider 使用同一个队列。

## 实现顺序

建议顺序：

1. 先实现通用 request/completion queue 和 `app_instance_id` 隔离。
   第一版 `app_runtime` helper 已完成。
2. bundle staging/registry 的桌面 mock 已实现，可通过 `jellyframe_cli.py registry`
   安装、枚举、解析和删除 `.jfapp`，并用原子 JSON 提交模拟 installed-app registry。
3. image decode mock/raw surface fixture、`AppImageSurfaceCache` 和 render-core image display command
   第一版已实现；Win32 debug 壳已能自动提交 mock decode 并重绘，并可从 `.jfapp`/源码包加载
   无压缩 24/32-bit BMP。通用 cache eviction、`object-fit` 子集和稳定失败原因 diagnostics
   已接入；下一步补产品级图片 codec。
4. 桌面 surface consumer 路径稳定后，在 ESP32-S3 port 中接 RGB565 小图/MJPEG decode，
   并严格限制尺寸和并发。
5. 接产品级 host-owned MP3 playback backend，仍只向 UI task 返回句柄和 ended/error 事件。mock 与极小
   `Audio()` JS 状态事件子集已经可用于桌面验证。
6. 更多面向用户的 JS API 必须在上述边界稳定后暴露。当前已暴露异步 `XMLHttpRequest` GET V0；
   `fetch()` 等有界 Promise/microtask 支持存在后再考虑；让 manifest/profile 检查拦截不支持目标。

这条顺序的核心目的很朴素：先把生命周期和调度做对，再逐步把真实硬件能力接进来。
