# 可选宿主服务接口契约

本文把图片/音频/轻量视频、网络数据请求和安装式 app bundle 的实现边界具体化。它是
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
completion 时过滤 stale instance。它不拥有 DOM、JS runtime、framebuffer 或平台线程。

`src/app_runtime/app_host.h` / `src/app_runtime/app_host.cpp` 提供更高一层的
`AppRuntimeHost`：把 lifecycle controller、request queue、completion queue 和 host handle
table 放进同一个有界容器，并提供“当前 app 提交 job / 分配 handle / 每帧 pump completion”的固定入口。
它还提供 `crash_current()`，用于宿主捕获 app 加载或运行错误后执行同一套资源释放规则。
它仍然不执行网络、文件、解码或 flash I/O；真实工作由桌面壳、RTOS worker 或 port 层完成。

`src/app_runtime/app_services.h` / `src/app_runtime/app_services.cpp` 提供第一版平台无关 mock：
`NetworkFetchMock`、`ImageDecodeMock`、`AppPrivateKvStorageMock` 和 `AudioCommandMock`。
它们用于桌面验证和端到端契约测试，仍不访问真实网络、文件系统、codec、音频设备或 flash；真实产品
host 应以相同 request/completion/handle 语义替换其 worker 实现。

同一组文件还提供第一版 manifest/profile gate：`AppServiceManifestCapabilities`、
`AppServiceHostProfile` 和 `app_service_policies_for_app(...)`。manifest 中的
`network.fetch`、`storage.kv` 或 `media.audio.mp3` 只表示 app 请求能力；只有被选中的
host/profile 同时允许该服务，并提供有界预算时，最终 runtime policy 才会启用。这样 JS binding 和
worker 实现都不需要各自散落权限判断。

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
- `HostServiceRequestQueue`：有界 request FIFO，支持 priority 选择、pending job 取消和按
  `app_instance_id` 批量取消。只负责单一 service kind 的 worker 应使用按 kind 过滤的 pop，
  避免 network、storage、image、media job 彼此误消费。
- `HostServiceCompletionQueue`：有界 completion FIFO，支持每帧按上限 pop，并可丢弃旧
  `app_instance_id` 的 completion。
- `HostHandleTable`：有界 host handle 表，使用 generation 防止释放后的旧 handle 误命中新资源，
  并统计 active count 与 used bytes。
- `AppSystemEventQueue`：有界宿主注入系统状态事件，事件绑定 active `app_instance_id`，并在帧边界消费。

## 通用 Job 结构

推荐使用单调递增的 32-bit job id。id 只需要在一次开机周期内唯一。

```cpp
enum class HostServiceJobKind {
    ImageDecode,
    AudioCommand,
    VideoFrameDecode,
    NetworkFetch,
    StorageKv,
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
- app rebuild/load 失败时，壳调用 `AppRuntimeHost::crash_current()` 释放资源，然后回到 system shell。

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
- 成功 completion 返回 `HostServiceHandleKind::Surface` handle；`AppDecodedSurfaceRecord`
  保存 width、height、stride、pixel format 和可选 raw pixels。
- `release_surface(...)` 必须由 UI/main task 在 surface 不再需要时调用，释放 record 和 host handle。
- render core 已提供 `ImageHandleResolver`、image display command 和 `ImagePainter`。宿主可以在
  layer tree 构建时把 `<img src>` 映射到 decoded surface handle，在 paint 阶段用 painter 绘制。
- Win32 browser debug 壳已接入 `app://icon` / `app://photo` raw RGB565 fixture：第一次 paint
  会提交 decode，completion 回到 UI/main task 后标记 `DomDirtyPaint` 并重绘。

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
- Win32 debug 壳会报告图片 request 拒绝和 completion 失败，包括触发的 `src`、submit/host
  状态和 host error code。
- render core 会把 `object-fit` 子集传给宿主 painter；Win32 debug painter 已支持
  `fill`、`contain`、`cover`、`none`、`scale-down`，默认居中。复杂 `object-position`
  延后。
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
- `HostMediaCapabilities::max_audio_streams` 通常在手表上设为 1。
- app 切换或锁屏策略触发时，系统 shell 可以停止或暂停 app audio。
- app teardown 会释放 host handles；具体服务实现也应在自己的生命周期 hook 中清掉 stale stream record，
  mock 通过 `collect_released_streams(...)` 覆盖这一路径。
- worker/audio task 不得调用 JS；只投递事件，由 UI task 派发 `ended`/`error` 等回调。

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

## App 私有 KV Storage 服务

存储只用于 app 私有小数据，不提供浏览器级持久同步 `localStorage`、cookie、IndexedDB、Cache API
或通用文件系统。目标是支持设置、token、小型 JSON 状态、离线缓存索引等常见嵌入式 app 需求。

当前 V0 mock：`AppPrivateKvStorageMock` 使用 app id 隔离命名空间，通过
`HostServiceJobKind::StorageKv` 异步完成 `get/set/remove/clear`。`get` 成功时返回
`StorageValue` handle；`set`、`remove` 和 `clear` 只返回状态。mock 会检查 key 长度、单 value
大小、每 app item 数和总 byte budget。

能力 gate：只有 app manifest 请求了 `storage.kv`，并且 host profile 提供启用状态的
`AppPrivateKvPolicy` 时，`policies.storage.enabled` 才为 true。key/value/item/byte 预算会被复制到
最终 mock 或产品 worker 使用的具体 storage policy 中。

`AppLocalStorageShadow` 是标准 `localStorage` V0 子集的小型内存 helper。它复用
`AppPrivateKvPolicy` 限制，用紧凑顺序表保存字符串 key/value，不执行任何宿主 I/O。当前
JerryScript binding 只有在宿主绑定这个非阻塞 shadow 时才暴露 `localStorage`；持久化、恢复和
flush/drop 策略仍属于宿主异步 storage 工作。

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

规则：

- 宿主使用 `push_current(...)` 为当前 app instance 注入事件。
- UI/main task 在帧边界通过 `pump_current(...)` 消费事件。
- `max_events_per_frame` 限制每帧事件处理量。
- 旧 app instance 的事件会被消费并丢弃。
- 队列本身不调用 JS、不修改 DOM、不读取 RTC/network/battery 硬件，也不直接触发 layout；后续 binding
  决定 accepted event 如何变成 app callback。
- 当前 JerryScript binding 将网络状态映射到 `navigator.onLine`，将 visibility 映射到
  `document.hidden`、`document.visibilityState` 和 `document` 的 `visibilitychange`。
  `window` 的 `online`/`offline` 事件和 battery JavaScript API 不进入 V0。
- Win32 debug 壳可以通过 `Ctrl+F6`/`Ctrl+F7`/`Ctrl+F8` 注入 fake event，方便 app 调试；
  硬件 port 应从自己的 host state provider 使用同一个队列。

## 实现顺序

建议顺序：

1. 先实现通用 request/completion queue 和 `app_instance_id` 隔离。
   第一版 `app_runtime` helper 已完成。
2. bundle staging/registry 的桌面 mock 已实现，可通过 `jellyframe_cli.py registry`
   安装、枚举、解析和删除 `.jfapp`，并用原子 JSON 提交模拟 installed-app registry。
3. image decode mock/raw surface fixture、`AppImageSurfaceCache` 和 render-core image display command
   第一版已实现；Win32 debug 壳已能自动提交 mock decode 并重绘，并可从 `.jfapp`/源码包加载
   无压缩 24/32-bit BMP。通用 cache eviction 和 `object-fit` 子集已接入；下一步补更细图片
   diagnostics 和产品级图片 codec。
4. 桌面 surface consumer 路径稳定后，在 ESP32-S3 port 中接 RGB565 小图/MJPEG decode，
   并严格限制尺寸和并发。
5. 接 host-owned MP3 playback，只返回句柄和 ended/error 事件。
6. 面向用户的 JS API 必须在上述边界稳定后暴露。当前已暴露异步 `XMLHttpRequest` GET V0；
   `fetch()` 等有界 Promise/microtask 支持存在后再考虑；让 manifest/profile 检查拦截不支持目标。

这条顺序的核心目的很朴素：先把生命周期和调度做对，再逐步把真实硬件能力接进来。
