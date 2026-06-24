# App Packaging

JellyFrame app packaging 会把 web-like 源文件转成确定性的、适合固件集成的 app 资源。
这里不应照搬手机或手表应用商店的安装包；JellyFrame 更适合保留小型类 Web 的开发体验，
然后在桌面离线生成无需文件系统、网络栈或复杂压缩归档即可加载的资源。

部署形态应分成两类：

- 静态 C++ 资源表用于固件内置 app：启动器、表盘、软件列表、出厂 app、bring-up fixture
  和安全兜底 UI。
- Flash 中的可安装 `.jfapp` bundle 才是第三方 app 的目标路径。V0 bundle 已能由桌面工具生成，
  并可由 Win32 壳和 CLI preview path 直接加载。如果第三方 app 必须靠重新烧录固件安装，JellyFrame 相比传统
  LVGL/C 固件 UI 的核心优势会消失。
- 启动器应被建模为带系统权限的 JellyFrame App 角色，而不是 runtime 内置页面。仓库提供
  `samples/apps/system/sample_launcher` 作为桌面 bring-up/CI 样例；产品宿主可以选择自己的
  受信 launcher app。

## 调研摘要

几个成熟平台有明显共性：

- Xiaomi Vela JS app 使用 `src/manifest.json` 描述应用身份、版本、权限、系统配置和页面路由。
  项目源码位于 `src/`，包含 `app.ux`、页面文件、公共资源和 i18n 文件，构建后输出包产物。
- Zepp OS 小程序使用根目录 `app.json` 描述 app 元信息、runtime 要求、权限、targets 和 i18n。
  不同设备的资源放在按 `targets` 命名的 assets 子目录中。
- HarmonyOS 应用包比 JellyFrame 需要的复杂得多，但它仍体现了 app 级配置、module 级配置和资源的分层。
- Android App Bundle 是发布格式，不是设备直接安装的运行时格式。它对 JellyFrame 最有价值的启发是：
  保留完整源包，再按目标设备生成更小的运行包。
- Apple bundle 使用标准目录层级和类似 `Info.plist` 的元信息，让代码能稳定找到资源。
- MSIX 使用显式 manifest 和文件映射/完整性元数据。完整性思路有价值，但容器和安装模型对 MCU 太重。

## 设计目标

- 源包对 Web 作者友好：HTML、CSS、classic JS、assets、fonts 和 i18n 文件。
- 构建输出可重复：资源排序、路径规范化、稳定生成 C++ table、debug 目录或 binary installable bundle。
- 运行时加载保持 O(log n) 或小包有界线性查找，不做高堆开销的归档解析。
- 部署前声明全部预算：resource bytes、DOM nodes、CSS rules、display commands、timers、listeners 和 framebuffer policy。
- 包资源加载继续无文件系统、无网络；宿主通过现有 `HostResourceLoader` 边界提供包内字节。
- 应用可以为运行时数据 API 声明网络能力。这个能力与包资源加载分离，因此仍禁止远程页面、
  远程 CSS/script/image 资源，但不会把未来宿主提供的 fetch API 路线写死。
- 可选资源缺失时干净降级；必需 entry 资源缺失应在烧录前由打包器报错。
- 打包默认执行字体资源预检，报告源码中使用的非 ASCII 字符、推荐 font profile 和 bitmap
  字体预算；开发者只有在明确知道目标环境字体覆盖由系统保证时才应使用 `--no-font-check`。

## 运行时安装与网络边界

JellyFrame 的第三方 app 目标是安装到 flash/外部存储的 bundle，而不是重新烧录固件。静态 C++
资源表仍然有价值，但它更适合启动器、表盘、系统设置、出厂 app 和安全兜底 UI。

安装式 bundle 的运行时边界：

- app manager/system shell 拥有安装、删除、升级和 app 列表刷新。
- 当前运行 app 的 JavaScript 不能直接挂载新 bundle，也不能修改全局资源索引。
- 下载、hash/签名校验、manifest/budget 校验和 flash 写入都属于宿主异步 job。
- 安装完成后通过 completion event 通知 UI，启动器下一帧刷新列表。
- 失败时 staging 区可直接丢弃，不能破坏已提交的 app 表。

运行时数据与存储边界：

- `permissions: ["network"]` / `capabilities: ["network.fetch"]` 只声明运行时数据请求。
- `capabilities: ["storage.kv"]` 只声明 app 私有小型异步 KV storage。
- `capabilities: ["media.audio.mp3"]` 声明可选的宿主持有 MP3 playback。当前 runtime
  已有 command/handle/completion 验证，Win32 壳可用 `--audio-smoke` 验证桌面 host adapter
  能接收本地或包内音频资源；JerryScript 构建已暴露带 `ended`/`error` 状态事件的宿主可选极小
  `Audio()` V0 子集，但仍不内置 MCU codec。
- app 可以请求天气、账号同步、小型 JSON 或下载由系统安装器继续验证的 `.jfapp`。
- 仍禁止把远程 HTML/CSS/script/image 当作页面资源直接加载。
- 仍不提供 cookie、浏览器级持久同步 `localStorage`、IndexedDB、Cache API 或通用文件系统；
  只有宿主绑定非阻塞 app 私有 shadow 时才暴露极小 `localStorage` V0。
- 目标 profile 可以按产品策略拒绝网络/storage/audio app，或限制最大响应字节、并发请求、超时、KV item
  数、byte 配额和 stream 数量。

这个设计保留未来“第三方安装式 app”的价值，同时避免 MCU 端变成一个不可控的通用浏览器 loader。

## 源包目录

推荐源包结构：

```text
my_app/
  jellyframe.app.json
  index.html
  styles/
    app.css
  scripts/
    app.js
  assets/
    icon.png
    images/
  fonts/
    ui.bdf
  i18n/
    en-US.json
    zh-CN.json
```

只有 `jellyframe.app.json` 和声明的 entry HTML 是必需的。其他文件均可选，并通过本地绝对路径或相对路径引用。

`README.md`、`README_zh.md`、隐藏目录、`__pycache__`、`.DS_Store` 和 `Thumbs.db`
等开发说明/系统文件会被 packer 忽略。它们可以用于说明源包，而不会扩大运行时资源或污染字体覆盖报告。

## Manifest V0

第一版 manifest 应保持很小，并使用 JSON。它由桌面工具消费，而不是由 MCU runtime 解析：

```json
{
  "$schema": "../../../tools/schemas/jellyframe.app.schema.json",
  "format": "jellyframe.app",
  "formatVersion": 0,
  "id": "com.example.weather",
  "name": "Weather",
  "role": "app",
  "version": {
    "name": "0.1.0",
    "code": 1
  },
  "entry": "/index.html",
  "runtime": {
    "minJellyFrame": "0.4.0",
    "script": "classic"
  },
  "viewport": {
    "designWidth": 300,
    "designHeight": 300,
    "shape": "round"
  },
  "budgets": {
    "maxResourceBytes": 131072,
    "maxDomNodes": 512,
    "maxCssRules": 256,
    "maxDisplayCommands": 512
  },
  "fonts": [
    {
      "id": "ui",
      "source": "fonts/ui.bdf",
      "profile": "app-subset-cn",
      "sizes": [16, 20],
      "weights": [400, 700]
    }
  ],
  "targets": {
    "esp32s3-round-300": {
      "viewport": {
        "width": 300,
        "height": 300,
        "shape": "round"
      },
      "fontProfile": "app-subset-cn",
      "output": "cpp"
    }
  },
  "permissions": ["network"],
  "capabilities": ["network.fetch"]
}
```

schema 位于 `tools/schemas/jellyframe.app.schema.json`。developer CLI 可以输出 schema 或其路径：

```powershell
python tools/jellyframe_cli.py schema --print-path
```

`role` 默认为 `app`。当前已保留 `launcher`、`watchface`、`settings` 等系统角色。声明系统角色或
`system.launcher`/`system.appManager` capability 并不会自动获得权限；它只表达 app 的意图，
真正授权必须由宿主/profile 根据签名、安装来源或产品策略决定。

`capabilities` 接受 JellyFrame 标准能力名，也接受产品私有扩展名。未知名称不再让 schema 直接拒绝
package，但如果所选产品 profile 没有通过 host policy 明确支持同名能力，工具仍会报告
`manifest-capability-unknown`。可移植 app 应优先使用标准名称；产品私有名称只应保留给无法用已文档化
Web-near 子集描述的服务。

`fonts` 当前是部署/工具声明，不是完整 CSS runtime font loading。打包器会把 `.jffont`、`.bdf`、
`.ttf`、`.otf`、`.woff` 等文件作为普通 `Font` 资源写入资源表或 `.jfapp`。其中 `.jffont`
V0/V1 已可由 runtime 解析并随 app instance 持有；`AppFontSet` 提供 bitmap fallback chain：
系统字体 profile 优先，app `.jffont` supplement 补缺字。Win32 壳可用 `--use-app-fonts`
显式切到这条验收路径。

manifest 中的每个 font 条目可以写：

```json
{
  "id": "ui-cn",
  "source": "fonts/ui-cn.jffont",
  "profile": "app-subset-cn",
  "family": "Jelly UI CN",
  "license": {
    "name": "OFL-1.1",
    "source": "Noto Sans SC subset",
    "url": "https://fonts.google.com/noto/specimen/Noto+Sans+SC"
  },
  "sizes": [16],
  "weights": [400, 700]
}
```

`family` 会被 package diagnostics 用来把显式 CSS `font-family` declaration 与 manifest runtime
font 匹配。`system-ui`、`sans-serif` 等 generic family 会报告为 generic fallback；未匹配的首选自定义
family 会产生 `font-family-unmatched`。`sizes`、`weights` 目前仍是产品策略元数据；runtime 文本后端选择
仍不实现完整浏览器 font-family cascade。工具仍会检查这些数组是否存在且合法，发布前可报告
`font-axis-metadata-missing` 或 `font-axis-metadata-invalid`。`license.name` 和 `license.source`
是推荐字段；缺失时 pack/check 会给出 `font-license-missing` 或 `font-license-incomplete`，
便于发布前确认字体来源。
`budgets.maxAppFonts`、`budgets.maxAppFontBytes`、`budgets.maxAppFontGlyphs` 会限制可用
runtime `.jffont` 数量、总字节和总 glyph 数；超过时会报告 `font-budget-exceeded`。这些检查只发生在工具层，
不会增加 MCU 渲染热路径成本。

`samples/apps/packages/jelly_font_policy` 是这条路径的最小验收样例：它在 CSS 和 manifest 元数据中声明
`"Jelly Tiny"`，并包含由仓库 BDF fixture 生成的极小 `.jffont` supplement。

当前稳定生产路径仍是：打包/检查阶段收集 used chars，离线从授权字体生成 bitmap glyph 数据，
再把生成的 `BitmapFont` 编译进 port/firmware 或作为 `.jffont` supplement 随 `.jfapp`
安装，并通过 `TextMeasureProvider`/`TextPainter` 或 `AppFontSet` 注入。

内置 target presets 位于 `tools/presets/targets`。可以这样列出：

```powershell
python tools/jellyframe_cli.py targets
```

内置 source-package 模板位于 `tools/templates/apps`。可以这样列出并创建：

```powershell
python tools/jellyframe_cli.py templates
python tools/jellyframe_cli.py new `
  --template calculator `
  --output build/my_calculator `
  --id org.example.calculator `
  --name Calculator `
  --target round-300
```

使用 `--target id` 时，packer 会先加载存在的 `tools/presets/targets/id.json`，再叠加
manifest 中同名 `targets[id]` 设置。只存在于 manifest 的自定义 target 也允许；
完全未知的 target 会明确失败。

影响运行兼容性的字段应由 packer 强制要求：

- `id`、`version.code`、`entry`、`runtime.minJellyFrame`；
- 全局或 target-specific viewport；
- `budgets.maxResourceBytes`；
- target 名称和输出类型。

`permissions: ["network"]` 和 `capabilities: ["network.fetch"]` 表示 app 期待未来宿主提供
运行时网络请求 API。它们不会启用远程包资源。packer 会把这些字段写入报告，产品固件可以据此拒绝
不符合板级策略的 app。

运行时应把这些 manifest 字段转换为 `AppServiceManifestCapabilities`，再与选中的
`AppServiceHostProfile` 通过 `app_service_policies_for_app(...)` 合成。某个 capability 被请求，
并不等于已经启用；host/profile 必须同时允许它，并提供有界预算。

## 资源路径规则

JellyFrame 应使用严格的本地路径子集：

- 包资源 URL 只能是本地路径：不接受 scheme、不接受 `//host`、不做 query 驱动的网络 fetch。
- 绝对 app 路径以 `/` 开头。
- 相对路径按引用它的资源目录解析。
- `.` 忽略；`..` 如果逃出 app root 就拒绝。
- 构建工具把分隔符规范化为 `/`，拒绝重复规范化路径，并按路径排序。
- CSS `url(...)`、`<link href>`、`<script src>`、图片和字体使用同一个 resolver。

这些规则与当前 ESP32-S3 bring-up 接近，也能让 MCU loader 很小。

## 构建输出

工具可以从同一份 manifest 生成两类输出。

桌面/调试输出：

```text
dist/my_app.jfdir/
  jellyframe.package.json
  resources...
```

固件内置输出：

```text
dist/my_app_resources.cpp
dist/my_app_resources.h
dist/my_app_font.h
dist/my_app_report.json
```

第三方安装输出：

```text
dist/my_app.jfapp
dist/my_app_report.json
```

静态 C++ table、debug 目录和 `.jfapp` binary bundle 向 runtime 暴露同一张逻辑资源表。
Renderer 和 script runtime 不应关心字节来自编译期 table、flash partition、debug 目录，
还是板级 app store。

生成的资源表应包含：

- 规范化路径；
- resource kind；
- byte pointer 或 blob offset；
- byte size；
- 可选 checksum，用于诊断；
- 可选 path hash，用于快速查找。

小包使用排序后的线性查找已经足够简单。较大包可以使用排序后的 FNV-1a path hash 加字符串确认，
避免 hash 碰撞导致难查的问题。

## `.jfapp` V0 Binary Format

`.jfapp` V0 是一个小端、未压缩、固定索引的二进制资源包。它不是 ZIP，也不是小型文件系统。
桌面工具可以一次性生成和完整校验；MCU port 可以把它放在 flash/外部存储中，按 offset 读取资源。

文件布局：

```text
header
manifest summary JSON
resource index[]
string table
payload blob
```

header 字段：

```c
char     magic[8];        // "JFAPPV0\0"
uint16_t header_size;     // 56
uint16_t format_version;  // 0
uint32_t flags;           // V0 must be 0
uint32_t summary_offset;
uint32_t summary_size;
uint32_t index_offset;
uint32_t resource_count;
uint32_t string_table_offset;
uint32_t string_table_size;
uint32_t payload_offset;
uint32_t payload_size;
uint32_t bundle_crc32;    // crc32 with this field zeroed
uint32_t reserved;        // V0 must be 0
```

每个资源索引条目固定 28 字节：

```c
uint32_t path_hash;       // FNV-1a over normalized UTF-8 app path
uint32_t path_offset;     // offset into string table
uint16_t path_size;
uint16_t kind;            // 0 other, 1 css, 2 classic js, 3 image, 4 font
uint32_t payload_offset;  // offset into payload blob
uint32_t payload_size;
uint32_t crc32;           // resource bytes
uint32_t flags;           // V0 must be 0
```

V0 明确不做压缩、加密、动态 native code、远程资源、依赖包和 MCU 端 JSON 全量解析。
manifest summary 是 packer 生成的紧凑 JSON，桌面工具和调试壳可读取；真正的嵌入式安装器可以
只解析需要的固定字段或使用打包报告/registry 生成的摘要。

## `.jffont` V0/V1 Binary Font Supplement

`.jffont` 是面向 `.jfapp` 动态字体补充的第一版二进制容器。它不是 TTF/OTF/WOFF
加载器，也不要求 MCU 端运行矢量字体 rasterizer；它保存的是已离线裁剪和 rasterize 的 bitmap glyph。
V0 保存紧凑的 1bpp glyph rows。V1 保持同一个固定表结构，并增加显式 2bpp/4bpp coverage rows，
用于字体级抗锯齿。当前工具可以生成该文件，render core 已提供只读内存 view 解析器，app runtime 也有按
`app_instance_id` 绑定的 `AppFontSet` 用于持有 `.jffont` bytes 并在 app 退出/切换时释放。
第一版 bitmap fallback chain 已实现：系统字体优先，然后 app supplement 补缺。CSS/runtime 激活语义和
产品级 text backend 选择仍在主线 C 中继续实现。

文件布局：

```text
header
glyph index[]
bitmap rows blob
```

header 固定 32 字节，小端：

```c
char     magic[8];          // "JFFONT0\0"
uint16_t header_size;       // 32
uint16_t format_version;    // 0 为 1bpp，1 为 coverage glyph
uint32_t glyph_count;
uint8_t  line_height;
uint8_t  fallback_advance;
uint16_t flags;             // V0: 0，V1: 低 8 位为 coverage bits 1/2/4
uint32_t glyph_table_offset;
uint32_t row_data_offset;
uint32_t row_data_size;
```

每个 glyph index 条目固定 16 字节，并按 Unicode codepoint 升序排列：

```c
uint32_t codepoint;
uint32_t row_offset;        // offset into bitmap rows blob
uint32_t row_size;
uint8_t  width;
uint8_t  height;
uint8_t  advance;
uint8_t  bytes_per_row;
```

这基本复用 `BitmapFontGlyph`/`BitmapFont` 的数据模型，但去掉了 C++ 指针和编译期符号。
`BitmapFontResource` 可以把 `.jffont` bytes 解析成只读 view，并复用现有二分 codepoint lookup
和 bitmap painter。Coverage glyph 是显式的画质/存储取舍：2bpp 大致让 row data 翻倍，
4bpp 大致让 row data 变为四倍；绘制时会对每个覆盖 glyph 像素多做一次小型 alpha coverage blend。
使用 V0/1bpp 字体的 app 不支付这部分成本。当前生成器从 BDF bitmap 邻近像素近似生成 coverage，
它不是运行时矢量 rasterizer，也不承诺浏览器级 subpixel typography。低层 view 要求调用方保证
`.jffont` bytes 在 view 生命周期内保持有效；
`AppFontSet::load_jffont` 会为当前 app instance 持有一份 bytes 拷贝，适合桌面/source-package
安全路径；`AppFontSet::attach_jffont_view` 和 `AppRuntimeHost::attach_current_jffont_view`
可直接挂载稳定的 flash/mmapped 或 `.jfapp` bundle payload，不复制字体 bytes。Win32 壳对可安装
`.jfapp` bundle 使用该 view 路径，对源目录仍回退到复制路径。
当前 `jellyframe_font_pack_gen` 支持同时生成固件静态 header 和 `.jffont`：

```powershell
jellyframe_font_pack_gen `
  --bdf app_font.bdf `
  --chars used_chars.txt `
  --output app_font.h `
  --output-binary app_font.jffont `
  --coverage-bits 4 `
  --name app_font
```

## 打包流水线

推荐桌面构建链：

1. 校验 `jellyframe.app.json` 并规范化路径。
2. 遍历 entry HTML、linked stylesheets 和 classic scripts。
3. 运行真实管线组件上报的 diagnostics。旧的文本检索式兼容性扫描已弃用；诊断应来自真正执行了
   parse、CSS、style、layout、layer、render、scripting 或 package loading 的组件。
4. 默认执行字体资源预检：收集源码字符、估算 bitmap font budget，并在提供 `--font-coverage`
   时验证目标字体覆盖。需要产出嵌入式 C++ 字体头文件时，再使用 `jellyframe_font_pack_gen`。
5. 执行预算检查：单资源字节、总包字节、CSS rule 估算和 script byte 限制。
6. 为选定 target 生成资源表。
7. 输出报告：warnings、管线 diagnostics、可选字体覆盖、估算内存和最终资源表。

## 明确裁剪

嵌入式包模型刻意不包含：

- MCU 端 runtime ZIP 解压或通用归档解析；
- 依赖包管理器；
- 动态 native libraries；
- 任意远程包资源；
- service workers、caches 或浏览器存储；
- 多进程安装/更新语义；
- 类似 Vela `.ux` 或 HarmonyOS ArkUI 的页面模块转译。

这些功能应放在 embedded core 之外，或等未来桌面 packaging 层需要时再做。

## 当前工具

推荐开发者入口是 `tools/jellyframe_cli.py`。仅校验：

```powershell
python tools/jellyframe_cli.py validate `
  --root samples/apps/packages/watch_weather `
  --report build/watch_weather_report.json
```

生成 C++ resource table 和 report：

```powershell
python tools/jellyframe_cli.py package `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --output-cpp build/watch_weather_resources.cpp `
  --report build/watch_weather_report.json
```

生成安装式 `.jfapp` bundle：

```powershell
python tools/jellyframe_cli.py package `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --output-bundle build/watch_weather.jfapp `
  --report build/watch_weather_report.json
```

生成给编辑器工具或人工检查使用的 debug 目录：

```powershell
python tools/jellyframe_cli.py package `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --output-cpp build/watch_weather_resources.cpp `
  --report build/watch_weather_report.json `
  --debug-dir build/watch_weather.jfdir
```

通过 Win32 shell capture path 预览源包：

```powershell
python tools/jellyframe_cli.py preview `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --output build/watch_weather.ppm
```

`package`、`check`、`preview` 和源码包 `install` 默认先运行 package validation，再通过临时 render-core
伪浏览器对 package entry HTML 跑一遍管线，让 parser/style/layout/layer diagnostics
来自真正处理 markup 和 CSS 的组件。`preview` 随后用 Win32 shell capture path 生成实际
app/package 图片。随后会默认运行字体资源预检，使用 `16x16` glyph 预算估算；可以用
`--font-budget WxH` 调整估算，用 `--font-coverage` 检查嵌入式字体覆盖，用
`--emit-used-chars` 写出 used chars 文件。只有明确不需要字体检查时才传入 `--no-font-check`；
`--skip-check` 会跳过包括管线和字体在内的开发预检。

CLI 会把 render-core 伪浏览器输出合并进指定 JSON report 的 `pipelineDiagnostics` 字段。任何 severity 为
`error` 的诊断都会使 `check`、`preview` 和 `package` 失败。warning 默认写入报告但不阻断；
CI 或发布打包希望 warning 也失败时，传入 `--strict`。

Package report 还包含 `fontDiagnostics`。这是工具层估算，不是运行时字体加载器：它会扫描
package 文本资源中出现的 codepoints，套用目标 `fontProfile`，解析 manifest 声明的 `.jffont`
V0 glyph table，并把剩余缺失的非 ASCII glyph 作为 warning 报告。`.ttf`、`.otf`、`.woff`、
`.bdf` 等字体文件可以作为文档或未来工具资源被打包，但当前不是 runtime-loadable font
supplement；如果写进 manifest `fonts`，会被报告为不支持的字体资源格式。

Windows 上做人类 app 开发时，应优先使用交互式 Win32 browser 壳：

```powershell
.\build\Release\jellyframe_win32_browser.exe --app samples\apps\packages\watch_weather
```

也可以直接打开 `.jfapp`，用于验证安装包与源目录渲染是否一致：

```powershell
.\build\Release\jellyframe_win32_browser.exe --app build\watch_weather.jfapp
.\build\Release\jellyframe_win32_browser.exe --capture build\watch_weather.bmp --app build\watch_weather.jfapp
```

如果需要验收 manifest `fonts` 中声明的 `.jffont` 是否真的可被当前包使用，给 Win32 壳加上
`--use-app-fonts`。默认路径仍使用 GDI 文本，方便桌面调试和截图审查。

```powershell
.\build\Release\jellyframe_win32_browser.exe --capture build\watch_weather_font.bmp --app build\watch_weather.jfapp --use-app-fonts
```

推荐的源码包安装路径是让 CLI 一次完成 validation、pipeline diagnostics、bundle 生成和 registry
安装：

```powershell
python tools/jellyframe_cli.py install `
  --store build/installed_apps `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --report build/watch_weather.install.report.json
```

随后 Win32 壳可以显示 installed-app registry，通过渲染出的 system-shell UI 启动 app，并删除非活动
app：

```powershell
.\build\Release\jellyframe_win32_browser.exe --registry-store build/installed_apps
.\build\Release\jellyframe_win32_browser.exe --registry-store build/installed_apps --launch-app org.jellyframe.examples.weather
.\build\Release\jellyframe_win32_browser.exe --capture build/app_manager.bmp --registry-store build/installed_apps
```

低层 registry helper 仍可用于安装已有 `.jfapp` bundle，或编写 registry 测试脚本：

```powershell
python tools/jellyframe_cli.py registry install `
  --store build/installed_apps `
  --bundle build/watch_weather.jfapp

python tools/jellyframe_cli.py registry list --store build/installed_apps

$bundle = python tools/jellyframe_cli.py registry path `
  --store build/installed_apps `
  --id org.jellyframe.examples.weather

.\build\Release\jellyframe_win32_browser.exe --app $bundle

python tools/jellyframe_cli.py registry remove `
  --store build/installed_apps `
  --id org.jellyframe.examples.weather
```

registry mock 会先校验 `.jfapp` header、section range、CRC32 和 manifest summary，再把 bundle
复制到 staging，最后用原子 JSON 写入提交 `registry.json`。Win32 system-shell 模式使用同一份
registry 格式完成 app discovery、launch 和非活动 app deletion。

render-core 伪浏览器继续作为独立 HTML/CSS 页面和 package entry 预检的确定性 CI 壳。
App/package 截图与人工调试应走 Win32 shell，这样同一路径能覆盖 package loading、scripting、
input 和 host-shell 行为。

对 package 文件运行校验、管线诊断和默认字体资源检查：

```powershell
python tools/jellyframe_cli.py check `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --report build/watch_weather_report.json
```

收集 package 使用到的字符，并可选择生成嵌入式 bitmap font header：

```powershell
python tools/jellyframe_cli.py font `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --report build/watch_weather_report.json `
  --used-chars build/watch_weather_used_chars.txt `
  --font-budget 16x16
```

如果已有 BDF 源字体，追加 `--bdf` 和 `--output-header` 生成固件静态 header：

```powershell
python tools/jellyframe_cli.py font `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --report build/watch_weather_report.json `
  --used-chars build/watch_weather_used_chars.txt `
  --font-budget 16x16 `
  --bdf path/to/ui.bdf `
  --output-header build/watch_weather_font.h `
  --name watch_weather_font
```

也可以生成 `.jffont`，作为 `.jfapp` 动态字体补充资源的二进制输入：

```powershell
python tools/jellyframe_cli.py font `
  --root samples/apps/packages/watch_weather `
  --target round-300 `
  --report build/watch_weather_report.json `
  --used-chars build/watch_weather_used_chars.txt `
  --bdf path/to/ui.bdf `
  --output-binary build/watch_weather_font.jffont
```

package report 会在 `serviceIntent` 中记录 manifest service intent。这个稳定摘要包含
`network.fetch`、`storage.kv`、`media.audio.mp3` 请求、`backgroundServices` 声明，以及提醒
工具和作者“最终授权仍由 host profile/product policy 决定”的 policy notes。如果 target preset 声明了
可选 `hostServices`，report 还会用 `targetSupport` 按 `supported`、`unsupported` 或 `unknown`
报告 network fetch、app 私有 KV 和 audio playback 的目标支持状态。如果 app 请求了被所选 target
明确标为 unsupported 的服务，package 会输出 `service-target-unsupported` warning；`unknown` 只作为
信息保留，因为很多产品 port 会在通用 preset 之外决定可选服务。桌面 app-runtime mock 可以验证
request/completion/handle 契约，`app_service_policies_for_app(...)` 会把这些请求与 host/profile
策略合成。核心不会执行真实网络或文件系统 I/O。

JSON report 面向 CI 和编辑器集成，包含 app 元信息、选中的 target config、effective budgets、
资源大小、CRC32/SHA-256 校验、service intent、local/remote reference 诊断、package-resource
warnings 和 `pipelineDiagnostics`。管线诊断包含伪浏览器格式/版本标记、输出 viewport、面向内存的管线统计、
severity 汇总，以及 parser、style、layout、layer、renderer 代码实际发出的 diagnostics。
已知不支持或降级的特性应给出明确原因；未知恢复至少应包含触发字段或片段。

`tools/package_app.py` 仍作为 CLI 和嵌入式构建集成使用的底层 packer。

内置 weather、clock、timer 和 calculator 模板都是普通 source package。它们覆盖
event delegation、`dataset`、timers、grid layout 和 form controls，不会在
HTML/CSS/JS 之上再引入一层 app framework。

可选 VS Code 辅助扩展位于 `tools/vscode-jellyframe`。它提供 manifest schema 关联，
并通过命令面板调用 `tools/jellyframe_cli.py` 完成 validate、check、preview、package
和模板创建，并提供启动所选 package 的 Win32 交互浏览器命令。它会读取 CLI report，
为 package warnings 和 `pipelineDiagnostics` 显示报告面板与 inline diagnostics。
CLI 仍是 CI 和非 VS Code 工作流的权威入口。

参考资料：

- Xiaomi Vela JS App project configuration 和 project structure：
  <https://iot.mi.com/vela/quickapp/en/guide/framework/manifest.html>，
  <https://iot.mi.com/vela/quickapp/en/guide/start/project-overview.html>
- Zepp OS Mini Program configuration 和 folder structure：
  <https://docs.zepp.com/docs/reference/app-json/>，
  <https://docs.zepp.com/docs/v2/guides/architecture/folder-structure/>
- HarmonyOS application package structure：
  <https://developer.huawei.com/consumer/en/doc/harmonyos-guides/application-package-structure-stage>
- Android App Bundle format：
  <https://developer.android.com/guide/app-bundle/app-bundle-format>
- Apple bundle resources：
  <https://developer.apple.com/documentation/BundleResources/placing-content-in-a-bundle>
- MSIX packaging：
  <https://learn.microsoft.com/en-us/windows/msix/package/packaging-uwp-apps>
