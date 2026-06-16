# JellyFrame Engine

JellyFrame 是一个面向低功耗可穿戴和嵌入式设备的深度裁剪 C++ HTML/CSS/JS
UI 运行时。它不是通用浏览器，而是一个“浏览器形状”的小型应用引擎：用 HTML 描述结构，
用 CSS 描述表现，并通过可选 JerryScript 桥接提供有界交互。

项目早期代号是 `WearWeb`；当前代码、target 和文档均使用 `JellyFrame`。

## 为什么做它

很多可穿戴 UI 栈要求开发者用类似 canvas 的 API 手绘所有界面。JellyFrame 尝试另一条路：
保留浏览器模型里便宜而有价值的部分，裁掉不适合小 MCU 的部分，并把显示、输入、文本和资源
交给清晰的宿主接口。

典型目标：

- 手表和小型仪表设备；
- 本地 HTML/CSS/JS 应用壳；
- 需要可维护 UI、但不能承担完整浏览器成本的嵌入式产品；
- 用于开发板 port 的桌面验证工具。

## 当前能力

- 容错 HTML tokenizer、tree builder 和紧凑 DOM。
- 面向嵌入式子集的 CSS parser、CSSOM 和 style resolver。
- 现代开发常用补充能力：CSS variables、有界 `@media`、保守 `@supports`、
  `:is()` / `:where()`、sibling selectors 和动态状态 pseudo-classes。
- Block、inline、简化 flex row/wrap 和响应式 grid-card layout。
- 按钮、文本输入、textarea、checkbox、radio、range、select、progress、meter 等控件。
- 命中测试、capture/target/bubble 事件流和平台无关输入。
- 可选 JerryScript runtime，提供小型 DOM/event/form/timer 绑定。
- Layer tree、display list、CPU rasterizer/compositor，以及面向 RGBA/BGRA、
  RGB565/BGR565、RGB332、Gray8、单色 buffer 的嵌入式 framebuffer adapter。
- DOM/CSSOM/style/render/layer/pipeline 检查、能力验证、字体包生成、截图和 Win32 交互验证工具。

精确的 can-do/cannot-do 契约见
[docs/developer_capability_matrix_zh.md](docs/developer_capability_matrix_zh.md)。

## 快速开始

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

把静态页面渲染成图片：

```powershell
.\build\Release\jellyframe_pseudo_browser.exe `
  examples\modern_cases\article_cards.html `
  examples\modern_cases\article_cards.css `
  article_cards.bmp 390 640
```

打开 Windows 交互验证壳：

```powershell
.\build\Release\jellyframe_win32_browser.exe `
  examples\app_cases\calculator.html `
  examples\app_cases\calculator.css
```

第一次接触项目时，请继续读 [HOW_TO_START_zh.md](HOW_TO_START_zh.md)。那里包含更完整的项目结构、
阅读顺序、编译运行方法，以及 Release 目录里每个 exe 的用途。

## 可选脚本构建

脚本能力是可选的。除非显式设置 `JELLYFRAME_BUILD_SCRIPTING=ON`，否则 `jellyframe_core`
不依赖 JerryScript。

```powershell
git clone --depth 1 https://github.com/jerryscript-project/jerryscript.git third_party\jerryscript
python third_party\jerryscript\tools\build.py --clean

cmake -S . -B build-script `
  -DJELLYFRAME_BUILD_SCRIPTING=ON `
  -DJERRYSCRIPT_ROOT="%CD%\third_party\jerryscript" `
  -DJERRYSCRIPT_LIBRARIES="%CD%\third_party\jerryscript\build\lib\MinSizeRel\jerry-core.lib;%CD%\third_party\jerryscript\build\lib\MinSizeRel\jerry-port.lib"
cmake --build build-script --config Release
```

脚本壳支持 classic inline scripts、宿主提供的本地外部 classic scripts、小型 DOM mutation API、
event listeners、表单属性和宿主泵动 timers。ES modules、网络加载和浏览器存储不属于嵌入式核心。

## 仓库结构

- `src/core`：平台无关核心。
- `src/script`：可选 JerryScript 绑定层。
- `examples`：检查工具、伪浏览器、Win32 browser 和验收页面。
- `tests`：平台无关回归测试。
- `benchmarks`：桌面微基准。
- `ports`：移植支撑代码和桌面 virtual-board 基准。
- `docs`：技术契约，以及仍在维护的状态/路线图文档。

## 文档入口

建议从这里开始：

- [HOW_TO_START_zh.md](HOW_TO_START_zh.md)：完整上手文档。
- [docs/README_zh.md](docs/README_zh.md)：文档索引，区分技术文档和当前项目过程文档。
- [docs/developer_capability_matrix_zh.md](docs/developer_capability_matrix_zh.md)：
  支持、降级、懒处理和延后功能。
- [docs/engine_architecture_zh.md](docs/engine_architecture_zh.md)：管线总览。
- [docs/embedded_hal_api_zh.md](docs/embedded_hal_api_zh.md)：开发板 port 的宿主/HAL 契约。
- [docs/project_status_zh.md](docs/project_status_zh.md)：当前主线状态和后续里程碑。

英文文档使用原文件名；中文文档使用 `_zh` 后缀。

## 版本

- 当前版本：`0.2.0-dev`，见 [VERSION](VERSION)。
- 变更记录：[CHANGELOG.md](CHANGELOG.md) 和 [CHANGELOG_zh.md](CHANGELOG_zh.md)。
- 版本规则：[docs/versioning_zh.md](docs/versioning_zh.md)。

## 当前状态

JellyFrame 目前适合小型本地嵌入式 UI 实验和桌面验证；不适合直接运行任意现代网站、
完整前端框架、联网浏览器应用或像素级兼容渲染。
