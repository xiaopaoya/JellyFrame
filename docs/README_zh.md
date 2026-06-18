# JellyFrame 文档索引

本目录保存技术文档：模块行为、支持子集、接口和宿主契约，应尽量贴近代码维护。

## 第一次阅读顺序

1. [../HOW_TO_START_zh.md](../HOW_TO_START_zh.md)
2. [engine_architecture_zh.md](engine_architecture_zh.md)
3. [developer_capability_matrix_zh.md](developer_capability_matrix_zh.md)
4. 如果要构建本地 app package，读 [app_packaging_zh.md](app_packaging_zh.md)。
5. 你要修改哪个模块，就读对应模块文档。

## 技术文档

### 核心管线

- [engine_architecture_zh.md](engine_architecture_zh.md)：类浏览器分层和当前取舍。
- [html_tokenizer_scope_zh.md](html_tokenizer_scope_zh.md)：tokenizer 子集、恢复规则和明确裁剪状态。
- [html_tree_builder_scope_zh.md](html_tree_builder_scope_zh.md)：DOM 构建子集和低性能设备限制。
- [html_parser_architecture_zh.md](html_parser_architecture_zh.md)：parser 结构、命名和性能规则。
- [css_parser_scope_zh.md](css_parser_scope_zh.md)：CSS syntax、at-rule 和 selector 子集。
- [cssom_scope_zh.md](cssom_scope_zh.md)：cascade 和 computed style 策略。
- [render_tree_scope_zh.md](render_tree_scope_zh.md)：render tree 构建和面向 layout 的规则。
- [layer_tree_scope_zh.md](layer_tree_scope_zh.md)：成层原因、裁剪和降级策略。
- [software_renderer_scope_zh.md](software_renderer_scope_zh.md)：CPU renderer、display commands 和明确裁剪。

### 交互与运行时

- [events_scope_zh.md](events_scope_zh.md)：hit testing、类 DOM event dispatch 和输入裁剪。
- [scripting_scope_zh.md](scripting_scope_zh.md)：当前 JerryScript 支持范围和不支持的浏览器 API。
- [run_loop_contract_zh.md](run_loop_contract_zh.md)：宿主运行循环顺序、dirty flags 和 repaint planning。

### 宿主、嵌入式和文本

- [host_abstraction_zh.md](host_abstraction_zh.md)：资源、时间、framebuffer、文本和预算的薄宿主边界。
- [app_packaging_zh.md](app_packaging_zh.md)：app 包格式、manifest 和资源包工作流。
- [../tools/vscode-jellyframe/README_zh.md](../tools/vscode-jellyframe/README_zh.md)：
  调用 developer CLI 的可选 VS Code 辅助扩展。
- [../schemas/jellyframe.app.schema.json](../schemas/jellyframe.app.schema.json)：
  `jellyframe.app.json` 的 JSON Schema。
- [embedded_hal_api_zh.md](embedded_hal_api_zh.md)：开发板侧 API checklist。
- [embedded_framebuffer_backend_zh.md](embedded_framebuffer_backend_zh.md)：调用方持有 framebuffer 的转换和 flush 契约。
- [text_backend_zh.md](text_backend_zh.md)：文本测量/绘制 API 和字体工作流。
- [embedded_optimization_notes_zh.md](embedded_optimization_notes_zh.md)：当前优化选择和基准基线。
- [porting_work_guide_zh.md](porting_work_guide_zh.md)：分阶段开发板移植指导。

### 面向开发者的功能契约

- [developer_capability_matrix_zh.md](developer_capability_matrix_zh.md)：最主要的 can-do/cannot-do 契约。
- [versioning_zh.md](versioning_zh.md)：版本和发布纪律。

## 双语维护

英文文档使用原文件名，中文文档使用 `_zh` 后缀，例如 `engine_architecture.md` 和
`engine_architecture_zh.md`。面向用户、架构、状态、发布和上手的文档应在同一次改动中维护双语版本。
