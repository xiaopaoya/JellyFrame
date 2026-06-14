# WearWeb Engine

WearWeb Engine 是一个面向低功耗可穿戴设备的深度裁剪 C++ HTML/CSS
运行时。第一阶段目标不是完整浏览器，而是一个小型、可移植的文档式 UI
引擎，后续可通过 JerryScript 承载 JavaScript app。

## 当前切片

- 最小 HTML tokenizer/parser
- DOM 树
- DOM mutation 原语，并带有面向未来 JavaScript 绑定的 dirty invalidation
- 轻量 CSS parser
- 支持标签、class、id 和 inline style 的基础级联
- 垂直 block layout，并带有简化 inline flow 与换行
- 稀疏 layer tree，并可 flatten 为平台无关 display list
- CPU software rasterizer/compositor，可输出 BMP/PPM 用于验证
- 核心 hit testing 和类 DOM event dispatch
- 平台无关 pointer/wheel input controller
- 轻量平台无关表单控件状态，覆盖 text input、textarea、checkbox、radio、range 和 select
- 可选 JerryScript runtime shell，用于脚本求值，并保持在 `wearweb_core` 之外
- 可选 JerryScript DOM/事件/表单桥接，用于小型宿主驱动应用
- 可选平台文本绘制回调，用于桌面验证
- Windows-only 交互式浏览器壳，用于观察和测试
- 控制台 demo

## 目标架构

```text
JS app
  |
JerryScript binding layer
  |
DOM + style + layout core
  |
Layer tree
  |
Display list / retained platform layers
  |
Platform renderer: CPU framebuffer, GDI, SDL, LVGL, custom wearable HAL
```

核心层刻意不依赖窗口系统、GPU API、字体系统、网络和操作系统服务。

## 构建

```powershell
cmake -S . -B build
cmake --build build
.\build\Debug\wearweb_demo.exe
```

默认 CMake 选项会构建例程、测试和基准：

- `WEARWEB_BUILD_EXAMPLES=ON`
- `WEARWEB_BUILD_TESTS=ON`
- `WEARWEB_BUILD_BENCHMARKS=ON`
- `WEARWEB_BUILD_SCRIPTING=OFF`

面向嵌入式或只构建核心库时：

```powershell
cmake -S . -B build-core -DWEARWEB_BUILD_EXAMPLES=OFF -DWEARWEB_BUILD_TESTS=OFF -DWEARWEB_BUILD_BENCHMARKS=OFF
cmake --build build-core --config Release
```

可选 JerryScript runtime 会构建为独立的 `wearweb_script` target。它默认关闭，
因此嵌入式/核心构建不会意外引入 JerryScript 头文件或库：

```powershell
git clone --depth 1 https://github.com/jerryscript-project/jerryscript.git third_party\jerryscript
python third_party\jerryscript\tools\build.py --clean

cmake -S . -B build-script `
  -DWEARWEB_BUILD_SCRIPTING=ON `
  -DJERRYSCRIPT_ROOT="%CD%\third_party\jerryscript" `
  -DJERRYSCRIPT_LIBRARIES="%CD%\third_party\jerryscript\build\lib\MinSizeRel\jerry-core.lib;%CD%\third_party\jerryscript\build\lib\MinSizeRel\jerry-port.lib"
cmake --build build-script --config Release
```

M6 脚本支持会执行 JavaScript，暴露很小的 `window`/`document` bridge，并允许脚本通过
`getElementById`、`createElement`、`createTextNode`、`appendChild`、`removeChild`、
attribute 和 `textContent` 修改 native DOM。它还会把 `addEventListener` / `removeEventListener`
桥接到现有 C++ 事件流，并暴露轻量表单控件属性（`value`、`checked`、`selectedIndex`）。
宿主泵动 timer 暴露 `setTimeout`、`clearTimeout`、`setInterval` 和 `clearInterval`。
自动 `<script>` 加载仍未支持。

通过 CTest 运行回归测试：

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

## 例程

```powershell
.\build\Debug\wearweb_demo.exe
.\build\Debug\wearweb_dom_dump.exe
.\build\Debug\wearweb_dom_dump.exe path\to\page.html
.\build\Debug\wearweb_cssom_dump.exe
.\build\Debug\wearweb_cssom_dump.exe path\to\style.css
.\build\Debug\wearweb_style_dump.exe path\to\page.html path\to\style.css
.\build\Debug\wearweb_render_tree_dump.exe path\to\page.html path\to\style.css
.\build\Debug\wearweb_layer_tree_dump.exe path\to\page.html path\to\style.css
.\build\Debug\wearweb_pipeline_dump.exe path\to\page.html path\to\style.css
.\build\Debug\wearweb_pseudo_browser.exe path\to\page.html path\to\style.css output.bmp 360 240
.\build\Debug\wearweb_pseudo_browser.exe examples\script_cases\runtime_probe.html examples\script_cases\runtime_probe.css output.bmp 360 240 --script examples\script_cases\runtime_probe.js
.\build\Debug\wearweb_pseudo_browser.exe examples\script_cases\dom_mutation_probe.html examples\script_cases\dom_mutation_probe.css output.bmp 360 260 --script examples\script_cases\dom_mutation_probe.js
.\build\Debug\wearweb_pseudo_browser.exe examples\script_cases\event_probe.html examples\script_cases\event_probe.css output.bmp 360 260 --script examples\script_cases\event_probe.js
.\build\Debug\wearweb_pseudo_browser.exe examples\app_cases\weather.html examples\app_cases\weather.css output.bmp 360 360 --script examples\app_cases\weather.js
.\build\Debug\wearweb_pseudo_browser.exe examples\app_cases\clock.html examples\app_cases\clock.css output.bmp 360 360 --script examples\app_cases\clock.js --pump-timers 3200
.\build\Debug\wearweb_win32_browser.exe path\to\page.html path\to\style.css
.\build\Debug\wearweb_win32_browser.exe examples\script_cases\event_probe.html examples\script_cases\event_probe.css --script examples\script_cases\event_probe.js
.\build\Debug\wearweb_win32_browser.exe examples\app_cases\calculator.html examples\app_cases\calculator.css --script examples\app_cases\calculator.js
.\build\Debug\wearweb_win32_browser.exe --capture output.bmp path\to\page.html path\to\style.css 390 640
.\build\Release\wearweb_microbench.exe 80 1000
.\build\Debug\wearweb_core_tests.exe
```

- `wearweb_demo`：运行当前 layout/layer/display-list 切片。
- `wearweb_dom_dump`：输出 tokenizer 结果和 ASCII DOM 树。文件输入限制为
  512 KiB，避免调试工具在低端目标上失控。
- `wearweb_cssom_dump`：输出解析后的 CSSOM style rules、specificity 和
  declarations。文件输入同样限制为 512 KiB。
- `wearweb_style_dump`：输出 form、input、button、dialog、card 等功能 UI
  节点的 computed style。
- `wearweb_render_tree_dump`：输出由 DOM 和 computed style 生成的 render tree。
- `wearweb_layer_tree_dump`：输出 layer 边界、成层原因、裁剪和 flatten 后的
  display-list 计数。
- `wearweb_pipeline_dump`：输出端到端 DOM/render/layout/layer/display-list
  计数和 display-list 预览。
- `wearweb_pseudo_browser`：运行当前完整管线并写出 BMP 或 PPM framebuffer
  图像。它是桌面验收工具，不是嵌入式 UI。它的内置 fallback 字体刻意保持极小；需要可读的
  UTF-8/中文文本验证时，请使用 Win32 browser 壳。在启用 scripting 的构建中，
  `--script` 会在绑定解析后的 DOM 后、渲染前执行一个外部 JavaScript 文件，并打印字符串化后的结果或异常。
  `--pump-timers ms` 会在渲染前推进宿主泵动 timer，便于无窗口 smoke test。
- `wearweb_win32_browser`：仅在 Windows 构建中可用。它打开一个 Win32/GDI
  交互窗口，使用同一套核心管线渲染，通过平台文本回调注入 GDI 文本绘制，并把鼠标和滚轮输入转发给平台无关
  input controller。它只用于桌面观察；核心仍保持窗口系统和操作系统无关。在 scripting 构建中，
  `--script` 会保持 JerryScript runtime 存活，使 JavaScript event listener 可以响应 native input，
  并在 DOM mutation 后重绘。
- `wearweb_win32_browser --capture`：通过同一条 Win32/GDI 文本路径渲染页面，并在不打开交互窗口的情况下写出
  BMP 或 PPM 文件。
- 检查工具和 Win32 壳会合并显式 CSS 文件、内嵌 `<style>` 元素和本地
  `<link rel="stylesheet">` 文件中的 author CSS。linked CSS 加载只存在于示例代码；核心只暴露
  callback，并保持平台无关。
- `wearweb_microbench`：运行 parser/render/layout/layer/flatten 微基准。有效性能数据应使用 Release build。
- `wearweb_core_tests`：唯一的平台无关回归测试程序。它覆盖 tokenizer/parser、DOM mutation、
  CSS parser、事件、hit testing、输入、render tree、layer tree 和 CPU framebuffer 行为。
  启用 scripting 的构建中，它还会包含 JerryScript runtime 测试。
- `examples/app_cases`：包含天气、时钟、计时器和计算器四个小型应用式验收页面。
  支持的开发子集和 M7 前评估见 `docs/embedded_app_subset_zh.md`。
- `docs/developer_capability_matrix_zh.md`：面向开发者的 can do/cannot do
  契约。依赖某个 HTML/CSS/DOM/script 功能前，优先查这里。
- `docs/memory_management_zh.md`：总结当前嵌入式内存行为、剩余风险和下一步 allocator/container 优化。

## 文档维护

英文文档使用原文件名，例如 `README.md` 和 `docs/roadmap.md`。中文文档使用
`_zh` 后缀，例如 `README_zh.md` 和 `docs/roadmap_zh.md`。

新增或更新面向用户、架构和发布的文档时，应同时维护两个版本。

## 版本与变更记录

- 当前版本：`0.2.0-dev`（记录在 `VERSION`）
- 英文变更记录：`CHANGELOG.md`
- 中文变更记录：`CHANGELOG_zh.md`
- 版本规则：`docs/versioning.md` 和 `docs/versioning_zh.md`
