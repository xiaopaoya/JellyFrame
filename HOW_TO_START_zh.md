# JellyFrame 上手指南

本文面向第一次打开这个仓库的开发者，说明 JellyFrame 是什么、仓库如何组织、如何编译运行、
Release 目录里每个 exe 是做什么的，以及应该按什么顺序阅读项目。

## 目录

- [1. JellyFrame 是什么](#1-jellyframe-是什么)
- [2. JellyFrame 不是什么](#2-jellyframe-不是什么)
- [3. 如何阅读这个项目](#3-如何阅读这个项目)
- [4. 仓库结构](#4-仓库结构)
- [5. 引擎管线](#5-引擎管线)
- [6. 构建要求](#6-构建要求)
- [7. 构建默认项目](#7-构建默认项目)
- [8. 运行测试和基准](#8-运行测试和基准)
- [9. 渲染页面并检查管线](#9-渲染页面并检查管线)
- [10. 可选 JerryScript 构建](#10-可选-jerryscript-构建)
- [11. Release EXE 都是干什么的](#11-release-exe-都是干什么的)
- [12. 有用的示例页面](#12-有用的示例页面)
- [13. 文档地图](#13-文档地图)
- [14. 常见开发流程](#14-常见开发流程)
- [15. 维护规则](#15-维护规则)

## 1. JellyFrame 是什么

JellyFrame 是一个面向嵌入式和可穿戴设备的小型 C++ 文档 UI 引擎。它保留了浏览器管线中
对应用开发很有价值的形状：

```text
HTML -> DOM -> CSSOM -> style -> render tree -> layout -> layer tree
     -> display list -> software rasterizer/compositor -> host framebuffer
```

可选 JerryScript 支持在上面提供一个小型 app runtime：

```text
classic script -> JerryScriptRuntime -> DOM/event/form/timer bindings
```

核心目标不是完整浏览器兼容，而是可用性优先的合理降级：常见 app 风格 HTML/CSS 即使失去
昂贵的现代增强，也应该保留结构、控件和可读样式。

## 2. JellyFrame 不是什么

JellyFrame 明确不提供：

- 网络加载、`fetch`、XHR、WebSocket；
- cookie、localStorage、IndexedDB 等浏览器存储；
- 完整 DOM、query selector APIs、Shadow DOM、Web Components；
- ES modules 或完整浏览器加载算法；
- Canvas、SVG、图片解码、视频；
- 完整 Flexbox/Grid/positioning/animation/filter 行为；
- GPU 合成或像素级浏览器兼容渲染。

依赖某个功能前，请先查
[docs/developer_capability_matrix_zh.md](docs/developer_capability_matrix_zh.md)。

## 3. 如何阅读这个项目

第一次阅读建议：

1. 读 [README_zh.md](README_zh.md)，建立项目直觉。
2. 读本文，了解构建、工具和运行方式。
3. 读 [docs/engine_architecture_zh.md](docs/engine_architecture_zh.md)，理解整条管线。
4. 写页面或 app 前，查 [docs/developer_capability_matrix_zh.md](docs/developer_capability_matrix_zh.md)。
5. 要改哪个模块，就读对应模块 scope 文档：tokenizer、tree builder、CSS parser、CSSOM、
   render tree、layer tree、renderer、events、scripting、text backend 或 HAL。
6. 用 dump 工具观察引擎实际生成了什么，不要只凭浏览器直觉猜。

做嵌入式移植时：

1. 读 [docs/embedded_hal_api_zh.md](docs/embedded_hal_api_zh.md)。
2. 读 [docs/porting_work_guide_zh.md](docs/porting_work_guide_zh.md)。
3. 从 `jellyframe_embedded_host_demo` 的结构开始照着接。
4. 实板不可用时，用 `ports/virtual_board` 估算 framebuffer 和 flush 成本。

## 4. 仓库结构

- `src/core`：平台无关核心。
- `src/script`：可选 JerryScript 绑定层。
- `examples`：桌面工具、伪浏览器、Win32 shell 和示例页面。
- `examples/modern_cases`：用于观察降级行为的现代 HTML/CSS 样例。
- `examples/script_cases`：runtime、DOM mutation、事件和脚本加载探针。
- `examples/app_cases`：天气、时钟、计时器、计算器应用样例。
- `examples/font_cases`：字体包样例输入。
- `tests`：平台无关回归测试源码。
- `benchmarks`：微基准。
- `ports/virtual_board`：桌面 virtual-board 成本估算。
- `ports/esp32s3-idf`：ESP32-S3 参考 bring-up 项目。
- `docs`：技术文档和模块/API 契约。
- `project_docs`：当前状态、路线图和开发过程说明。

## 5. 引擎管线

核心模块大致对应这些文件：

- HTML tokenizer：`src/core/html_tokenizer.*`
- HTML tree builder/parser：`src/core/html_tree_builder.*`、`src/core/html_parser.*`
- DOM 和 dirty flags：`src/core/dom.*`
- CSS parser 和 CSSOM 数据：`src/core/css_parser.*`、`src/core/style.*`
- 外链样式/脚本收集 helper：`src/core/document_style.*`、`src/core/document_script.*`
- Render tree 和 layout：`src/core/render_tree.*`、`src/core/layout.*`
- Layer tree 和 hit testing：`src/core/layer_tree.*`、`src/core/hit_test.*`
- Events 和 input：`src/core/event.*`、`src/core/input.*`
- 表单控件：`src/core/form_control.*`
- 软件渲染：`src/core/software_renderer.*`
- 文本后端契约：`src/core/text_backend.*`
- Host/HAL 契约和预算：`src/core/host.h`、`src/core/budget.h`
- 嵌入式 framebuffer adapter：`src/core/embedded_framebuffer.*`
- Frame update 和 dirty rectangles：`src/core/frame_update.*`、`src/core/dirty_region.*`
- 可选脚本：`src/script/jerryscript_runtime.*`

当前实现偏向简单数据结构、有界恢复、显式预算和可预测 fallback，而不是完整 Web 平台行为。

## 6. 构建要求

必需：

- CMake
- C++17 编译器
- Windows 上建议使用 Visual Studio 或 Visual Studio Build Tools 的 MSVC

可选：

- 启用脚本构建时需要 JerryScript 源码和已构建库
- 生成嵌入式字体包时需要 BDF bitmap font；先用
  `jellyframe_capability_check --font-budget WxH` 选择字体 profile
- 开发 `ports/esp32s3-idf` 时需要 ESP-IDF

下面命令使用 PowerShell，因为当前工作区是 Windows。

## 7. 构建默认项目

配置并构建：

```powershell
cmake -S . -B build
cmake --build build --config Release
```

默认选项：

- `JELLYFRAME_BUILD_EXAMPLES=ON`
- `JELLYFRAME_BUILD_TESTS=ON`
- `JELLYFRAME_BUILD_BENCHMARKS=ON`
- `JELLYFRAME_BUILD_SCRIPTING=OFF`

只构建核心库或面向嵌入式的构建：

```powershell
cmake -S . -B build-core `
  -DJELLYFRAME_BUILD_EXAMPLES=OFF `
  -DJELLYFRAME_BUILD_TESTS=OFF `
  -DJELLYFRAME_BUILD_BENCHMARKS=OFF
cmake --build build-core --config Release
```

## 8. 运行测试和基准

运行回归测试：

```powershell
ctest --test-dir build -C Release --output-on-failure
```

运行核心微基准：

```powershell
.\build\Release\jellyframe_microbench.exe
```

运行 virtual board 基准：

```powershell
.\build\Release\jellyframe_virtual_bench.exe 300 300 60 200 80 0.85 40
```

有效性能数据应使用 Release build。

## 9. 渲染页面并检查管线

不打开窗口，把页面渲染成 BMP 或 PPM：

```powershell
.\build\Release\jellyframe_pseudo_browser.exe `
  examples\modern_cases\article_cards.html `
  examples\modern_cases\article_cards.css `
  article_cards.bmp 390 640
```

打开 Windows 交互 shell：

```powershell
.\build\Release\jellyframe_win32_browser.exe `
  examples\app_cases\calculator.html `
  examples\app_cases\calculator.css
```

通过 Win32/GDI 文本路径截图：

```powershell
.\build\Release\jellyframe_win32_browser.exe --capture `
  calculator.bmp `
  examples\app_cases\calculator.html `
  examples\app_cases\calculator.css `
  390 640
```

检查中间结构：

```powershell
.\build\Release\jellyframe_dom_dump.exe examples\modern_cases\search_home.html
.\build\Release\jellyframe_cssom_dump.exe examples\modern_cases\search_home.css
.\build\Release\jellyframe_style_dump.exe examples\modern_cases\search_home.html examples\modern_cases\search_home.css
.\build\Release\jellyframe_render_tree_dump.exe examples\modern_cases\search_home.html examples\modern_cases\search_home.css
.\build\Release\jellyframe_layer_tree_dump.exe examples\modern_cases\search_home.html examples\modern_cases\search_home.css
.\build\Release\jellyframe_pipeline_dump.exe examples\modern_cases\search_home.html examples\modern_cases\search_home.css
```

嵌入前扫描页面能力：

```powershell
.\build\Release\jellyframe_capability_check.exe `
  examples\app_cases\weather.html `
  examples\app_cases\weather.css `
  examples\app_cases\weather.js
```

## 10. 可选 JerryScript 构建

拉取并构建 JerryScript：

```powershell
git clone --depth 1 https://github.com/jerryscript-project/jerryscript.git third_party\jerryscript
python third_party\jerryscript\tools\build.py --clean
```

启用 JellyFrame scripting：

```powershell
cmake -S . -B build-script `
  -DJELLYFRAME_BUILD_SCRIPTING=ON `
  -DJERRYSCRIPT_ROOT="%CD%\third_party\jerryscript" `
  -DJERRYSCRIPT_LIBRARIES="%CD%\third_party\jerryscript\build\lib\MinSizeRel\jerry-core.lib;%CD%\third_party\jerryscript\build\lib\MinSizeRel\jerry-port.lib"
cmake --build build-script --config Release
```

运行带脚本页面：

```powershell
.\build-script\Release\jellyframe_pseudo_browser.exe `
  examples\app_cases\weather.html `
  examples\app_cases\weather.css `
  weather.bmp 360 360 --script examples\app_cases\weather.js
```

运行 timer 驱动页面：

```powershell
.\build-script\Release\jellyframe_pseudo_browser.exe `
  examples\app_cases\clock.html `
  examples\app_cases\clock.css `
  clock.bmp 360 360 --script examples\app_cases\clock.js --pump-timers 3200
```

scripting shell 会自动收集 inline classic scripts 和宿主可加载的本地 classic
`<script src>` 文件。对于把 JavaScript 放在 HTML 旁边、但没有从 HTML 链接的示例，
可以继续使用 `--script extra.js`。

## 11. Release EXE 都是干什么的

下表中的 exe 在相关 CMake 选项启用时生成到 `build\Release`。

| 程序 | 用途 |
| --- | --- |
| `jellyframe_demo.exe` | 小型控制台 demo，运行当前核心管线切片。 |
| `jellyframe_dom_dump.exe` | 输出 tokenizer 结果和 ASCII DOM 树。用于排查 HTML 解析异常。 |
| `jellyframe_cssom_dump.exe` | 输出 CSS rules、specificity 和 declarations。用于调试 parser 和 cascade。 |
| `jellyframe_style_dump.exe` | 输出功能 UI 节点的 computed style。 |
| `jellyframe_render_tree_dump.exe` | 输出由 DOM 和 style 生成的 render tree。 |
| `jellyframe_layer_tree_dump.exe` | 输出 layer 边界、成层原因、裁剪和 flatten 后 display-list 数量。 |
| `jellyframe_pipeline_dump.exe` | 输出端到端 DOM/render/layout/layer/display-list 计数和 display-list 预览。 |
| `jellyframe_pseudo_browser.exe` | 跑完整管线并写出 BMP/PPM 图片。最适合无交互验收。 |
| `jellyframe_win32_browser.exe` | Windows-only 交互验证壳，使用 Win32/GDI 文本测量和绘制。 |
| `jellyframe_capability_check.exe` | 扫描 HTML/CSS/JS 的支持子集、降级特性和不支持 API，也可做字体覆盖检查。 |
| `jellyframe_font_pack_gen.exe` | 把 BDF bitmap font 和 used-character list 转成 C++ `BitmapFont` header。 |
| `jellyframe_embedded_host_demo.exe` | 平台无关 bring-up demo，使用静态资源、bitmap text 和 RGB565 framebuffer。 |
| `jellyframe_microbench.exe` | 跑 parser/render/layout/layer/flatten 微基准。 |
| `jellyframe_virtual_bench.exe` | 跑 desktop virtual-board benchmark，并估算 RGB565 flush 成本。 |
| `jellyframe_core_tests.exe` | 单一平台无关回归测试程序。 |

注意：

- `jellyframe_pseudo_browser.exe` 也支持
  `--app package_dir output.ppm`，用于包含 `jellyframe.app.json` 的 M11 app 源包目录。
- `jellyframe_win32_browser.exe` 只在 Windows 上构建。
- scripting 构建中，`jellyframe_pseudo_browser.exe`、`jellyframe_win32_browser.exe` 和
  `jellyframe_core_tests.exe` 会链接可选脚本 target。
- 检查工具和 Win32 shell 会合并显式 CSS 文件、内嵌 `<style>` 和宿主可加载的本地
  `<link rel="stylesheet">`。核心本身不做文件 I/O。

## 12. 有用的示例页面

- `examples/apps/watch_weather`：第一份 M11 app package 示例，包含
  `jellyframe.app.json`、本地 HTML/CSS/classic JS，并声明 future runtime data request
  所需的 network capability。
- `examples/modern_cases`：观察现代 HTML/CSS 合理降级的样例。
- `examples/script_cases`：runtime、DOM mutation、事件和脚本加载探针。
- `examples/app_cases/weather.*`：select 驱动的天气面板。
- `examples/app_cases/clock.*`：timer 驱动时钟。
- `examples/app_cases/timer.*`：计时器/秒表 UI。
- `examples/app_cases/calculator.*`：按钮驱动计算器。

把 M11 示例打包成生成式资源表：

```powershell
python tools\jellyframe_cli.py validate `
  --root examples\apps\watch_weather `
  --target round-300 `
  --report build\watch_weather_report.json
```

```powershell
python tools\jellyframe_cli.py package `
  --root examples\apps\watch_weather `
  --target round-300 `
  --output-cpp build\watch_weather_resources.cpp `
  --report build\watch_weather_report.json `
  --debug-dir build\watch_weather.jfdir
```

通过伪浏览器渲染：

```powershell
python tools\jellyframe_cli.py preview `
  --root examples\apps\watch_weather `
  --target round-300 `
  --output build\watch_weather.ppm
```

同时运行 package 校验和 capability check：

```powershell
python tools\jellyframe_cli.py check `
  --root examples\apps\watch_weather `
  --target round-300 `
  --report build\watch_weather_report.json `
  --font-budget 16x16
```

收集 package 的非 ASCII 字符，供嵌入式 bitmap font pack 使用：

```powershell
python tools\jellyframe_cli.py font `
  --root examples\apps\watch_weather `
  --target round-300 `
  --report build\watch_weather_report.json `
  --used-chars build\watch_weather_used_chars.txt `
  --font-budget 16x16
```

输出 manifest schema 路径，便于编辑器配置：

```powershell
python tools\jellyframe_cli.py schema --print-path
```

列出 target presets：

```powershell
python tools\jellyframe_cli.py targets
```

如果页面渲染不对，建议按这个顺序排查：

1. `jellyframe_capability_check`
2. `jellyframe_dom_dump`
3. `jellyframe_cssom_dump`
4. `jellyframe_style_dump`
5. `jellyframe_render_tree_dump`
6. `jellyframe_layer_tree_dump`
7. `jellyframe_pipeline_dump`
8. `jellyframe_pseudo_browser` 或 `jellyframe_win32_browser --capture`

## 13. 文档地图

文档索引见 [docs/README_zh.md](docs/README_zh.md)。

最重要的技术文档：

- [docs/developer_capability_matrix_zh.md](docs/developer_capability_matrix_zh.md)
- [docs/engine_architecture_zh.md](docs/engine_architecture_zh.md)
- [docs/html_tokenizer_scope_zh.md](docs/html_tokenizer_scope_zh.md)
- [docs/html_tree_builder_scope_zh.md](docs/html_tree_builder_scope_zh.md)
- [docs/css_parser_scope_zh.md](docs/css_parser_scope_zh.md)
- [docs/cssom_scope_zh.md](docs/cssom_scope_zh.md)
- [docs/render_tree_scope_zh.md](docs/render_tree_scope_zh.md)
- [docs/layer_tree_scope_zh.md](docs/layer_tree_scope_zh.md)
- [docs/software_renderer_scope_zh.md](docs/software_renderer_scope_zh.md)
- [docs/events_scope_zh.md](docs/events_scope_zh.md)
- [docs/scripting_scope_zh.md](docs/scripting_scope_zh.md)
- [docs/text_backend_zh.md](docs/text_backend_zh.md)
- [docs/embedded_hal_api_zh.md](docs/embedded_hal_api_zh.md)

仍在维护的过程文档：

- [project_docs/project_status_zh.md](project_docs/project_status_zh.md)
- [project_docs/roadmap_zh.md](project_docs/roadmap_zh.md)
- [docs/run_loop_contract_zh.md](docs/run_loop_contract_zh.md)
- [project_docs/memory_management_zh.md](project_docs/memory_management_zh.md)
- [project_docs/dom_arena_feasibility_zh.md](project_docs/dom_arena_feasibility_zh.md)
- [CHANGELOG_zh.md](CHANGELOG_zh.md)

## 14. 常见开发流程

新增 HTML parser 行为时：

1. 补 tokenizer/tree-builder 测试。
2. 用 `jellyframe_dom_dump` 跑最小页面。
3. 如果支持子集变化，更新对应 scope 文档。

新增 CSS 支持时：

1. 保证不支持值不会覆盖已支持 fallback。
2. 补 parser/style resolver 测试。
3. 跑 `jellyframe_cssom_dump` 和 `jellyframe_style_dump`。
4. 更新 capability matrix 和 capability checker。

修改 layout 或 rendering 时：

1. 增加聚焦的回归测试。
2. 对比 `render_tree_dump`、`layer_tree_dump` 和 `pipeline_dump`。
3. 通过 `jellyframe_pseudo_browser` 渲染 BMP。
4. 文本质量敏感时，用 Win32 capture 验证。

新增 scripting API 时：

1. 先确认 C++ 核心能在没有浏览器服务的情况下兑现行为。
2. JS wrapper 保持为 native DOM node 的非拥有视图。
3. 每个 JerryScript value 都要清晰 retain/release。
4. 补 scripting 测试，并更新 [docs/scripting_scope_zh.md](docs/scripting_scope_zh.md)。

## 15. 维护规则

- 本文要保持实用，帮助新开发者不用先读完整仓库就能运行和观察项目。
- CMake target 变化时，更新 exe 表格。
- 文档移动、归档或新增时，更新文档地图。
- 同步维护英文版 [HOW_TO_START.md](HOW_TO_START.md)。
