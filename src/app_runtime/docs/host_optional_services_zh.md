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
`NetworkFetchMock` 和 `AppPrivateKvStorageMock`。它们用于桌面验证和端到端契约测试，仍不访问真实网络、
文件系统或 flash；真实产品 host 应以相同 request/completion/handle 语义替换其 worker 实现。

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
  `app_instance_id` 批量取消。
- `HostServiceCompletionQueue`：有界 completion FIFO，支持每帧按上限 pop，并可丢弃旧
  `app_instance_id` 的 completion。
- `HostHandleTable`：有界 host handle 表，使用 generation 防止释放后的旧 handle 误命中新资源，
  并统计 active count 与 used bytes。

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

推荐 request：

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

推荐 result handle 指向：

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
- cache 满时可以 LRU 回收未被当前 display list 引用的 surface。
- 如果解码失败，页面保留占位盒并报告 diagnostics。

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
- 播放自然结束时投递 `Completed`。
- 错误时投递 `Failed` 和 host error code。

规则：

- `HostMediaCapabilities::max_audio_streams` 通常在手表上设为 1。
- app 切换或锁屏策略触发时，系统 shell 可以停止或暂停 app audio。
- worker/audio task 不得调用 JS；只投递事件，由 UI task 派发 `ended`/`error` 等回调。

## 轻量视频/MJPEG 服务

当前只建议作为实验性 frame provider，不承诺 `<video>`。

推荐范围：

- 低分辨率 MJPEG。
- 帧输出 RGB565。
- 固定或较低 fps，例如 10-15fps。
- 不与主 UI 帧率绑定；掉帧优先于阻塞 UI。

规则：

- H.264 不进入 ESP32-S3 默认 profile。
- video frame buffer 由宿主持有，UI 只拿最新 frame handle。
- 如果解码积压，丢弃旧帧，不追赶历史帧。
- 若 dirty repaint 无法跟上，暂停视频或降低 fps。

## 网络数据服务

网络只用于 runtime data API，不用于页面资源 loader。

当前 V0 mock：`NetworkFetchMock` 用固定 fixture 模拟 `network.fetch`，通过
`HostServiceJobKind::NetworkFetch` 提交 request，completion 返回 `FetchResponse` handle。
它会检查 capability、URL 长度、响应 byte budget 和 request queue 上限。响应 body 由 mock
持有，UI/main task 只能通过 handle 查询并显式释放。

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

存储只用于 app 私有小数据，不提供浏览器级同步 `localStorage`、cookie、IndexedDB、Cache API
或通用文件系统。目标是支持设置、token、小型 JSON 状态、离线缓存索引等常见嵌入式 app 需求。

当前 V0 mock：`AppPrivateKvStorageMock` 使用 app id 隔离命名空间，通过
`HostServiceJobKind::StorageKv` 异步完成 `get/set/remove/clear`。`get` 成功时返回
`StorageValue` handle；`set`、`remove` 和 `clear` 只返回状态。mock 会检查 key 长度、单 value
大小、每 app item 数和总 byte budget。

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
- JS API 暴露前先完成配额、错误码、崩溃恢复和测试；不要实现同步阻塞式 `localStorage`。

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

## 实现顺序

建议顺序：

1. 先实现通用 request/completion queue 和 `app_instance_id` 隔离。
   第一版 `app_runtime` helper 已完成。
2. bundle staging/registry 的桌面 mock 已实现，可通过 `jellyframe_cli.py registry`
   安装、枚举、解析和删除 `.jfapp`，并用原子 JSON 提交模拟 installed-app registry。
3. 实现 image decode mock：用桌面库或预生成 raw surface 验证 `<img>`/图标生命周期。
4. 在 ESP32-S3 port 中接 RGB565 小图/MJPEG decode，并严格限制尺寸和并发。
5. 接 host-owned MP3 playback，只返回句柄和 ended/error 事件。
6. 最后再暴露 JS `fetch` 或 media API，并让能力检查/manifest profile 拦截不支持目标。

这条顺序的核心目的很朴素：先把生命周期和调度做对，再逐步把真实硬件能力接进来。
