# App Packaging

日期：2026-06-17

M11 的目标是把临时示例资源升级成可重复的 app packaging 流程。这里不应照搬手机或手表应用商店的安装包；
JellyFrame 更适合保留小型类 Web 的开发体验，然后在桌面离线生成适合固件读取的资源表。
MCU 端不应解析文件系统、网络资源或复杂压缩归档。

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
- 构建输出可重复：资源排序、路径规范化、稳定生成 C++ 或 binary table。
- 运行时加载保持 O(log n) 或小包有界线性查找，不做高堆开销的归档解析。
- 部署前声明全部预算：resource bytes、DOM nodes、CSS rules、display commands、timers、listeners 和 framebuffer policy。
- 包资源加载继续无文件系统、无网络；宿主通过现有 `HostResourceLoader` 边界提供包内字节。
- 应用可以为运行时数据 API 声明网络能力。这个能力与包资源加载分离，因此 M11 仍禁止远程页面、
  远程 CSS/script/image 资源，但不会把未来宿主提供的 fetch API 路线写死。
- 可选资源缺失时干净降级；必需 entry 资源缺失应在烧录前由打包器报错。

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

## Manifest V0

第一版 manifest 应保持很小，并使用 JSON。它由桌面工具消费，而不是由 MCU runtime 解析：

```json
{
  "$schema": "../../../schemas/jellyframe.app.schema.json",
  "format": "jellyframe.app",
  "formatVersion": 0,
  "id": "com.example.weather",
  "name": "Weather",
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

schema 位于 `schemas/jellyframe.app.schema.json`。developer CLI 可以输出 schema 或其路径：

```powershell
python tools/jellyframe_cli.py schema --print-path
```

内置 target presets 位于 `presets/targets`。可以这样列出：

```powershell
python tools/jellyframe_cli.py targets
```

使用 `--target id` 时，packer 会先加载存在的 `presets/targets/id.json`，再叠加
manifest 中同名 `targets[id]` 设置。只存在于 manifest 的自定义 target 也允许；
完全未知的 target 会明确失败。

影响运行兼容性的字段应由 packer 强制要求：

- `id`、`version.code`、`entry`、`runtime.minJellyFrame`；
- 全局或 target-specific viewport；
- `budgets.maxResourceBytes`；
- target 名称和输出类型。

`permissions: ["network"]` 和 `capabilities: ["network.fetch"]` 表示 app 期待未来宿主提供
运行时网络请求 API。它们不会启用远程包资源。M11 packer 会把这些字段写入报告，产品固件可以据此拒绝
不符合板级策略的 app。

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

M11 应从同一份 manifest 生成两类输出。

桌面/调试输出：

```text
dist/my_app.jfdir/
  jellyframe.package.json
  resources...
```

嵌入式输出：

```text
dist/my_app_resources.cpp
dist/my_app_resources.h
dist/my_app_font.h
dist/my_app_report.json
```

生成的资源表应包含：

- 规范化路径；
- resource kind；
- byte pointer 或 blob offset；
- byte size；
- 可选 checksum，用于诊断；
- 可选 path hash，用于快速查找。

小包使用排序后的线性查找已经足够简单。较大包可以使用排序后的 FNV-1a path hash 加字符串确认，
避免 hash 碰撞导致难查的问题。

## 打包流水线

推荐桌面构建链：

1. 校验 `jellyframe.app.json` 并规范化路径。
2. 遍历 entry HTML、linked stylesheets 和 classic scripts。
3. 对解析出的资源集合运行 `jellyframe_capability_check`。
4. 使用 `jellyframe_font_pack_gen` 生成或验证 bitmap font pack。
5. 执行预算检查：单资源字节、总包字节、CSS rule 估算和 script byte 限制。
6. 为选定 target 生成资源表。
7. 输出报告：warnings、不支持功能、字体覆盖、估算内存和最终资源表。

## 明确裁剪

M11 暂不应加入：

- MCU 端 runtime ZIP 解压或通用归档解析；
- 依赖包管理器；
- 动态 native libraries；
- 任意远程包资源；
- service workers、caches 或浏览器存储；
- 多进程安装/更新语义；
- 类似 Vela `.ux` 或 HarmonyOS ArkUI 的页面模块转译。

这些功能应放在核心之外，或等未来桌面 packaging 层需要时再做。

## 建议的 M11 实现顺序

1. 增加平台无关的 app manifest parser/validator，作为桌面工具。
2. 把 ESP32-S3 resource generator 提升为顶层可复用工具。
3. 为任意源包生成兼容 `ResourceBundle` 的 C++ table。
4. 将生成的资源集合接入 capability check 和 font-pack 生成。
5. 让 pseudo browser 能按 manifest 打开 package directory。
6. 增加一个 weather/clock/calculator package sample，覆盖 HTML/CSS/JS、fonts 和 assets。

## 当前工具

推荐开发者入口是 `tools/jellyframe_cli.py`。仅校验：

```powershell
python tools/jellyframe_cli.py validate `
  --root examples/apps/watch_weather `
  --report build/watch_weather_report.json
```

生成 C++ resource table 和 report：

```powershell
python tools/jellyframe_cli.py package `
  --root examples/apps/watch_weather `
  --target round-300 `
  --output-cpp build/watch_weather_resources.cpp `
  --report build/watch_weather_report.json
```

生成给编辑器工具或人工检查使用的 debug 目录：

```powershell
python tools/jellyframe_cli.py package `
  --root examples/apps/watch_weather `
  --target round-300 `
  --output-cpp build/watch_weather_resources.cpp `
  --report build/watch_weather_report.json `
  --debug-dir build/watch_weather.jfdir
```

伪浏览器可以直接打开源包目录：

```powershell
python tools/jellyframe_cli.py preview `
  --root examples/apps/watch_weather `
  --target round-300 `
  --output build/watch_weather.ppm
```

对 package 文件运行校验和 capability check：

```powershell
python tools/jellyframe_cli.py check `
  --root examples/apps/watch_weather `
  --target round-300 `
  --report build/watch_weather_report.json `
  --font-budget 16x16
```

伪浏览器会报告 manifest 是否请求了网络能力，但目前还不会执行网络请求。

JSON report 面向 CI 和编辑器集成，包含 app 元信息、选中的 target config、effective budgets、
资源大小、CRC32/SHA-256 校验、local/remote reference 诊断和 package-resource warnings。

`tools/package_app.py` 仍作为 CLI 和嵌入式构建集成使用的底层 packer。

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
