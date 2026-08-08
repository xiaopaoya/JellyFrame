# Retained Rendering 与 Tile/Scanline 范围

> 最后更新：2026-08-04；适用版本：0.5.0

本文定义两个历史遗留问题的工程边界：结构变化时的 retained layout/display-list diff，
以及不持有完整 framebuffer 的 tile/scanline renderer。它是 Render Core 的设计契约，
不是某个 port 的验收报告。

## 当前事实

- Render Core 已保留 render tree、layout tree、layer tree 和 display list 的跨帧存储。
- `FrameUpdatePlan` 对 paint-only、稳定布局和旧/新 layout dirty bounds 有增量路径。
- `DirtyRegionOptions` 可以比较旧/新 layer tree，并能合并 transient overlay 的旧/新边界。
- DOM 结构变化仍保守回退 full-frame。当前没有完整的 retained layout diff 或 display-list
  diff，也没有声明已经支持子树 display command 重用。
- 默认软件 compositor 以完整逻辑 RGBA framebuffer 为输出目标；嵌入式 adapter 可以把
  dirty rect 转换为小 strip，但这不等于 renderer 已经支持无 framebuffer 的 tile/scanline。

这些边界是刻意的安全策略：如果无法证明旧命令的身份、顺序、裁剪、资源和覆盖关系，错误地
重用旧命令比多画一帧更容易产生残影、穿透、错误滚动条或错误弹层。

## 第一阶段：只做可证明的候选测量

第一阶段不改变绘制结果，不复用命令，先建立可量化证据：

- 为 render/layout/layer pipeline 生成稳定的 subtree fingerprint，输入只包含公开的
  结构、computed paint/layout 结果、资源版本和子树顺序；输出使用固定宽度整数。
- 在新树生成后与上一帧候选匹配，统计 `candidate_hits`、`candidate_misses`、重复 key、
  资源失效、bounds/clip 变化和保守回退原因。
- 只保留有界的索引和 counters；未启用 retained candidate profile 的 app 不分配缓存。
- benchmark 必须同时测结构插入/删除、文本变化、图片 completion、渐变/阴影、透明/重叠、
  transform/opacity 动画和滚动容器。

第一阶段的成功条件是能指出真实候选比例和回退原因，而不是宣称已经支持 display-list
diff。只有候选匹配稳定且可证明不改变像素，才进入第二阶段。

## 第二阶段：受限 subtree replay

仅考虑同时满足以下条件的候选：

- subtree key 唯一且跨帧有效；
- layout bounds、clip、transform、opacity、scroll offset 与资源版本一致；
- 子树及其前后可能覆盖者都属于不透明、无 overlap 的安全集合；
- display command 数量、缓存字节数和 dirty area 均低于 profile 预算；
- 缓存失效、尺寸变化、异步资源变化和 tree teardown 都有集中清理路径。

缓存命中失败必须与未启用缓存完全相同地走现有 pipeline。默认 profile 先保持关闭；开启后
必须有像素 capture、随机 mutation 和长帧回归，且证明默认 app 的内存与 clean frame 成本
没有回退。

## Tile/scanline 评估门槛

Tile/scanline 是输出模型变化，不是简单的 present adapter。它只有在目标设备同时满足以下
条件时才立项实现：

- 完整逻辑 framebuffer 或其必要的工作集无法在目标 profile 预算内容纳；或实测 framebuffer
  保留/转换导致明确的内存或帧时间失败；
- 现有 dirty rect、scroll strip、strip conversion 和 port-local DMA 已经排除；
- 测试能提供同一页面的峰值 internal RAM、PSRAM、tile scratch、frame time、p95、回退数和
  像素一致性证据；
- 目标场景包含文本、圆角、透明、渐变、图片、阴影和滚动，不能只用纯色矩形证明可行。

实验接口应保持平台无关，例如调用方提供有界 `TileRenderTarget` 或 scanline sink，核心
只写当前 tile/行并在提交前完成 clip-aware 合成。接口不能暴露 DMA、panel、GRAM、缓存
地址或 MCU intrinsic。未启用该 profile 时不创建 tile scratch，也不改变默认 framebuffer
路径。

Tile/scanline 的正确性风险包括跨 tile 的圆角/阴影覆盖、渐变坐标连续性、透明 layer 的
中间结果、文本行和 glyph cache、图片解码生命周期以及滚动后的旧像素清理。任何不支持的
命令必须明确回退到完整 framebuffer 或拒绝该 profile，不能静默产生局部错误。

## 近期结论

- P3 硬件验收期间不接入完整 retained diff 或 tile renderer。
- 0.5 只保留当前 dirty-region、scroll-blit 和 framebuffer/strip sink 契约。
- 0.6 先做平台无关 candidate fingerprint + benchmark；没有真实净收益就不做命令缓存。
- Tile/scanline 作为独立的 0.6/1.0 架构门槛项，必须由真实内存证据触发，并与 retained
  diff 分开验收，避免两个高风险变量同时进入固件。
