# WearWeb Engine

WearWeb Engine 是一个面向低功耗可穿戴设备的深度裁剪 C++ HTML/CSS
运行时。第一阶段目标不是完整浏览器，而是一个小型、可移植的文档式 UI
引擎，后续可以通过 JerryScript 承载 JavaScript app。

## 当前切片

- 最小 HTML tokenizer/parser
- DOM 树
- 轻量 CSS parser
- 支持标签、class、id 和 inline style 的基础样式解析
- 垂直 block layout
- 稀疏 layer tree，并可 flatten 为平台无关 display list
- CPU software rasterizer/compositor，可输出 BMP/PPM 用于验收
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

面向嵌入式或只构建核心库时：

```powershell
cmake -S . -B build-core -DWEARWEB_BUILD_EXAMPLES=OFF -DWEARWEB_BUILD_TESTS=OFF -DWEARWEB_BUILD_BENCHMARKS=OFF
cmake --build build-core --config Release
```

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
.\build\Release\wearweb_microbench.exe 80 1000
.\build\Debug\wearweb_tokenizer_tests.exe
.\build\Debug\wearweb_css_parser_tests.exe
.\build\Debug\wearweb_render_tree_tests.exe
.\build\Debug\wearweb_layer_tree_tests.exe
.\build\Debug\wearweb_software_renderer_tests.exe
```

- `wearweb_demo`：运行当前 layout/layer/display-list 垂直切片。
- `wearweb_dom_dump`：输出 tokenizer 结果和 ASCII DOM 树。文件输入限制为
  512 KiB，避免调试工具在低端目标上输出失控。
- `wearweb_cssom_dump`：输出解析后的 CSSOM style rules、specificity 和
  declarations。文件输入同样限制为 512 KiB。
- `wearweb_style_dump`：输出 form、input、button、dialog、card 等功能 UI
  节点的 computed style。
- `wearweb_render_tree_dump`：输出由 DOM 和 computed style 生成的 render tree。
- `wearweb_layer_tree_dump`：输出 layer 边界、成层原因、裁剪和 flatten 后
  display-list 计数。
- `wearweb_pipeline_dump`：输出端到端 DOM/render/layout/layer/display-list 计数和
  display-list 预览。
- `wearweb_pseudo_browser`：运行当前完整管线并写出 BMP 或 PPM framebuffer 图像。
  它是桌面验收壳，不是嵌入式 UI。
- `wearweb_microbench`：运行 parser/render/layout/layer/flatten 微基准。有效性能数据应
  使用 Release build。
- `wearweb_tokenizer_tests`：运行当前 tokenizer 和 parser 回归检查。
- `wearweb_css_parser_tests`：运行 CSS parser 和 fallback style 回归检查。
- `wearweb_render_tree_tests`、`wearweb_layer_tree_tests` 和
  `wearweb_software_renderer_tests`：覆盖 render、layer 与 CPU framebuffer 行为。

## 文档维护

英文文档使用原文件名，例如 `README.md`、`docs/roadmap.md`。
中文文档使用 `_zh` 后缀，例如 `README_zh.md`、`docs/roadmap_zh.md`。
新增或更新面向用户/架构的文档时，应同时维护两个版本。

## 版本与变更记录

- 当前版本记录在 `VERSION`
- 英文变更记录：`CHANGELOG.md`
- 中文变更记录：`CHANGELOG_zh.md`
- 版本规则：见 `docs/versioning.md` 和 `docs/versioning_zh.md`
