# 路线图

当前完整状态、职责边界和下一阶段定义见 `project_status_zh.md`。本文只保留高层路线。

## 里程碑 1：静态文档核心

- HTML 子集：document 节点、文本、常见 block 和 inline 标签
- CSS 子集：简单选择器和少量属性
- Layout：block flow、文本测量抽象
- Rendering：稀疏 layer tree、裁剪，以及包含矩形、文本和图片占位符的 display list

状态：基本完成，并且当前 app-oriented 子集已经超过最初计划。

## 里程碑 2：嵌入式渲染后端

- 软件 framebuffer 后端：已可用于验证
- 脏矩形重绘：第一版自动 `dirty_region` 子集已可用于非结构性 DOM 变化；display
  invalidation 诊断已能统计 dirty rectangles 覆盖的 layer/display command，完整 retained
  display-list reuse 延后
- 嵌入式 framebuffer adapter：已支持调用方持有的 RGBA8888/BGRA8888、RGB565/BGR565、
  RGB332、Gray8 和单色 buffer
- 平台文本测量/绘制后端：API 已有，Win32/GDI 验证后端已接入，第一版静态 embedded bitmap
  backend 和 BDF pack 生成器已可用；LVGL/vendor 引擎只应包装成很薄的 text/panel/input hooks，
  不作为 JellyFrame 主渲染器
- 指针/触摸输入路由：已有 pointer/wheel 核心；button/crown focus navigation 已有第一版 core API；
  仍需要开发板 adapter
- 平台无关开发板 bring-up 形态：已通过 `jellyframe_embedded_host_demo` 提供静态资源/RGB565
  demo

## 里程碑 3：App runtime

- JerryScript 集成：已有可选 scripting build
- DOM mutation APIs：已可用
- Timer/event loop：已有宿主泵动 timer
- Classic document script loading：已在 scripting 示例壳中可用
- Resource abstraction：壳层已有 callback 形式的本地 stylesheet/classic script 加载；网络/fetch
  仍明确不属于核心
- Device capability APIs：第一版 `HostDeviceCapabilities` 契约已可用；更深的自动适配延后
- 集中式 host budgets：已贯穿 parser、render、layout、layer、display-list、dirty-region 和 scripting 限制

## 里程碑 4：可穿戴 UI 能力

- 小屏 viewport model
- 适配表冠、按键、触摸的 focus/navigation model：核心已有第一版焦点遍历和激活 API
- 低功耗动画调度

## 里程碑 4 Packaging 线

M11 是可穿戴 app runtime 中的打包部分。目标开发体验应先做 CLI，再做 VS Code
集成；独立 IDE 先延后，等 CLI 和预览壳稳定后再判断是否值得做。

已完成或已有第一版：

- `jellyframe.app.json` V0 manifest 形态，覆盖 identity、entry、runtime、
  viewport、budgets、targets、permissions 和 capabilities
- 包资源保持 local-only；运行时网络能力单独声明，留给未来宿主 API
- 顶层 `tools/jellyframe_cli.py` developer CLI，支持 validate/package/preview
  和 package-scoped capability checks
- `tools/package_app.py` packer，可校验 package，并输出 C++ 资源表和 JSON report
- `schemas/jellyframe.app.schema.json`，用于编辑器和 CI 校验 manifest
- 内置 `round-300`、`rect-320x240` 和 `esp32s3-round-300` target presets
- CLI `font` 工作流，可校验 package、输出 used characters，并可从 BDF 输入生成
  bitmap font header
- 内置 weather、clock、timer 和 calculator app 模板，并可通过
  `jellyframe_cli.py new` 创建
- pseudo browser `--app` 源包预览路径
- `examples/apps/watch_weather` 第一份 package sample
- ESP32-S3 bring-up 资源已改用顶层 packer

下一步 packaging 工作：

1. 在硬件需求更明确后继续补充 target presets。
2. 在 CLI 之上做 VS Code extension：schema association、一键 preview/package、报告面板和
   inline capability warnings。
3. 只有当 CLI/plugin 工作流无法满足非程序员 app 作者时，再考虑独立可视化工具。

## 兼容性短线：现代 CSS authoring 子集

- 现代语法分析报告中最高收益的项目已经落地：`var()` fallback resolution、有界条件
  `@media`、动态 pseudo-classes、`:is()` / `:where()`、sibling selectors 和
  简化 flex grow/shrink/basis sizing
- 保守的 `@supports (property: value)` query 展开已经可用
- 有界 `relative`/`absolute`/`fixed` positioned layout 已可用于常见 app overlay
- 这条短线后续主要是围绕已支持 declaration 继续补测试
- 继续延后：完整 `:has()`、完整 `@container`、完整动画/filter/image pipeline 和浏览器级完整
  layout 算法

## 兼容性短线：HTML parser 与 DOM 直觉

HTML Living Standard 降级审计指出的若干缺口，比旧浏览器兼容模式更影响 app 作者的日常直觉。
采纳项应优先减少“写了正常 HTML 却得到意外 DOM”的情况，同时不引入 quirks mode、
`document.write()`、speculative parser 或完整 adoption-agency 算法。

第一批采纳：

- 修正非 void 元素自闭合斜杠语义：`<div/>` 在 HTML 中仍应按 start tag 处理，真正的 void
  元素继续保持叶子节点。
- tree construction 阶段不再折叠普通文本空白；保留文本节点，由 layout/rendering 根据受支持的
  CSS 子集处理空白。
- 将 `textarea` 和 `title` 作为近似 RCDATA 处理，支持字符引用解码；`script` 和 `style`
  继续保持 raw-text。
- 扩充常用 HTML named character references，并收紧分号、属性上下文和 numeric reference
  恢复规则。
- 为 node/depth/attribute 预算触发增加 parser 降级诊断：继续允许优雅截断，但必须让调用方可观察。
- 引入最小文档元数据模型，记录 doctype 和可选 comment，并保留未来升级到
  `Document`/`Comment`/`DocumentType` 节点的路径，前提是不破坏现有 DOM ownership。

第二批采纳：

- 定义最小 insertion-mode 子集：before-html、before-head、in-head、after-head、in-body。
- 增加 fragment parsing，用于 `innerHTML`、template fragment 和组件片段。
- 增加 `template.content` 风格的 inert fragment ownership。
- 扩大常用 implied-end-tag 行为，覆盖 `p`、`select`/`option` 和 `optgroup` 等场景。
- 增加有界 table tree-construction 子集：`table`/`tbody`/`thead`/`tfoot`/`tr`/`td`/`th`。
- 改善 classic script raw-text 边界，并让不支持的 module script 产生显式诊断，避免静默惊讶。

采纳但谨慎：

- 最小 inline SVG/foreign-content 边界识别有价值，但完整 foreign-content parsing 延后。
- 完整 quirks/limited-quirks、parser reentrant `document.write()`、speculative parser、
  完整 adoption-agency 和完整 table foster parenting 继续不进入当前范围；除非项目转向通用浏览器。

## 推荐下一步顺序

1. 先落地第一批 HTML parser/DOM 兼容项，优先选择便宜且能直接减少 app 作者惊讶的项目。
2. 收敛核心运行循环和 dirty update 契约，补充长时间 timer/input smoke。
3. M9 的有界 invalidation/诊断已完成；后续按真实数据决定是否加入 retained subtree reuse。
4. M10 文本后端 adapter 和字体工作流在当前主线范围内已完成。
5. 补齐本地资源包工具和 app packaging。
6. 继续内存/allocator 优化，包括 `DomOwner` 原型和 detached-node instrumentation。
7. 只有在目标硬件确实需要时，再推进 tiled/scanline presentation。
