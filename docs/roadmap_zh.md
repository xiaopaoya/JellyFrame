# 路线图

当前完整状态、职责边界和下一阶段定义见 `docs/project_status_zh.md`。本文只保留高层路线。

## 里程碑 1：静态文档核心

- HTML 子集：document 节点、文本、常见 block 和 inline 标签
- CSS 子集：简单选择器和少量属性
- Layout：block flow、文本测量抽象
- Rendering：稀疏 layer tree、裁剪，以及包含矩形、文本和图片占位符的 display list

状态：基本完成，并且当前 app-oriented 子集已经超过最初计划。

## 里程碑 2：嵌入式渲染后端

- 软件 framebuffer 后端：已可用于验证
- 脏矩形重绘：第一版自动 `dirty_region` 子集已可用于非结构性 DOM 变化；仍需要更细的
  layer/display-command invalidation
- 嵌入式 framebuffer adapter：已支持调用方持有的 RGBA8888/BGRA8888、RGB565/BGR565、
  RGB332、Gray8 和单色 buffer
- 平台文本测量/绘制后端：API 已有，Win32/GDI 验证后端已接入，第一版静态 embedded bitmap
  backend 和 BDF pack 生成器已可用；仍需要 LVGL/vendor adapters
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
- App 打包格式

## 兼容性短线：现代 CSS authoring 子集

- 现代语法分析报告中最高收益的项目已经落地：`var()` fallback resolution、有界条件
  `@media`、动态 pseudo-classes、`:is()` / `:where()` 和 sibling selectors
- 保守的 `@supports (property: value)` query 展开已经可用
- 剩余低成本工作：简化 flex sizing，以及有界 `absolute`/`fixed` positioned layout
- 继续延后：完整 `:has()`、完整 `@container`、完整动画/filter/image pipeline 和浏览器级完整
  layout 算法

## 推荐下一步顺序

1. 完成低成本现代 CSS authoring 子集，下一步从简化 flex sizing 和有界 positioned layout 开始。
2. 收敛核心运行循环和 dirty update 契约，补充长时间 timer/input smoke。
3. 做更细的 invalidation/subtree reuse，减少脚本交互后的全管线重建。
4. 完善文本后端 adapter 和字体工作流验证，保持 bitmap font 作为默认低成本路线。
5. 补齐本地资源包工具和 app packaging。
6. 继续内存/allocator 优化，包括 `DomOwner` 原型和 detached-node instrumentation。
7. 只有在目标硬件确实需要时，再推进 tiled/scanline presentation。
