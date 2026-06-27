# 嵌入式优化说明


目前尚未明确目标 CPU、内存布局、显示控制器和指令集，所以当前优化集中在小型可穿戴设备通用的重要约束上。

## 当前选择

- Parser 热路径不依赖异常。
- Parser 阶段使用带明确上限的线性扫描。
- DOM、CSS 和诊断文件输入都有上限。
- DOM parsing 将 tokenizer 输出流式送入 tree builder。
- Render tree 在 layout 前过滤非可见节点。
- Layout 只负责几何；绘制组织交给稀疏 layer tree。
- Layer tree 稀疏成层，普通盒子绘制进父 layer。
- Layer flatten 后的 display list 使用简单 rectangle 和 text commands。
- 边框被输出为 fill rectangles。
- 不支持的现代 CSS 在 block/rule 边界跳过，避免恢复循环。
- Style cascade slots 使用固定数组，不为每个节点创建级联 hash map。
- Style resolution 在 `StyleResolver` 内缓存有界的 id/class/tag 候选规则集合；最终选择器匹配仍逐节点执行，
  因此 descendant、child 和 attribute selector 语义保持正确。
- DOM attributes 使用紧凑顺序 `AttributeList`，不再为每个节点维护 attribute hash map。
- DOM event listener storage 惰性分配，没有 listener 的节点不携带空 listener table。
- DOM dirty bits 会向祖先传播，因此根节点 dirty 检查为 O(1)，dirty 清理会跳过干净子树，
  且同值 `textContent` 赋值不会触发重绘。已有唯一 text child 的元素会原地更新该 child，
  避免计数器、计时器标签等常见路径制造结构性 dirty。
- DOM 子树销毁和整子树 `textContent` 替换使用显式工作列表，而不是递归销毁子节点，
  降低极深生成式文档在小栈设备上的风险。
- 脚本 timer 由宿主泵动，带 callback budget，并有明确的 JerryScript reference 释放路径。
- 平台文本绘制通过可选回调注入；核心 renderer 保留可移植 bitmap fallback，不链接 Win32/GDI。
- software rasterizer 对不透明矩形使用直接行填充。
- offscreen compositing 在像素循环前裁剪 source/destination rectangles。
- `embedded_framebuffer` 只把裁剪后的 dirty rectangles 转换到调用方持有的显示 buffer；
  它不分配、不持有，也不自行 flush 设备内存。
- `embedded_framebuffer` 现在按 rectangle 进行格式分派转换，并可通过
  `EmbeddedFrameBufferPresentStats` 可选报告 converted pixels、packed bytes、clipped/empty rects
  和 flush count。
- `HostFrameSink::present` 被定义为 frame-lifetime 边界；如果底层 DMA 异步刷新，宿主必须在返回前确保
  buffer 已可复用，或在 UI loop 中等待 flush-done 后再进入下一帧。
- `FrameScratch` 和 `AppFrameScratch` 提供帧级临时容器复用。dirty-region bounds、dirty rectangles、
  animation style overrides 和 host completion batch/accepted list 可以跨帧保留 capacity，每帧清空；
  睡眠、切换 app 或内存告急时可显式 `release()` 归还容量。
- 响应式 grid 子集使用有界整数 auto-placement、clamped span 和紧凑的逐行
  occupancy bit mask，而不是完整 track-sizing engine。
- `MonotonicArena` 已可用于文档生命周期分配。Render tree、layout tree 和 layer tree builder
  都提供 arena-backed 路径，并已接入嵌入式 benchmark。
- Arena 统计会同时报告 used bytes 和 block capacity，因此 benchmark 日志可以区分真实管线数据
  与块式分配余量。

## 内存建议

- 如果目标栈很小，应替换递归析构/遍历。
- 评估 DOM nodes 是否应进入 document arena。
- 继续使用 `AttributeList`，除非测量证明需要微型 id/class 索引；紧凑 UI 节点不维护 hash buckets 更省内存。
- CSS rules 已按 id/class/tag/universal bucket 建索引，并复用有界候选规则缓存；完整 computed-style
  sharing 仍延后到继承和 mutation invalidation 能保持简单之后。
- 在小 RAM 系统上限制 layer/display-list 输出，或按 dirty region 分块生成。
- 不要把整屏 RGB565 target 固定放进 internal RAM，除非硬件和 heap watermark 证明足够。优先使用 PSRAM
  持久 framebuffer/target，或用小 DMA-capable strip buffer 做按 dirty rect 分段转换。
- 区分跨帧保留和帧内临时内存：DOM、stylesheet、form state、decoded surface cache、持久 framebuffer、
  retained layout/layer tree 会跨帧保留；parser scratch、dirty rect 临时列表、host completion 临时列表、
  offscreen compositing buffer 和 strip conversion buffer 应在帧边界释放或复用。
- 推荐 UI loop 持有一个 `FrameScratch` 和一个 `AppFrameScratch`。常规帧调用 `begin_frame()` / `end_frame()`
  复用容器；息屏、app exit、system shell 切换或 heap watermark 过低时调用 `release()`。
- JellyFrame 可以主动释放/复用上述纯软件临时容器，但不能安全释放真实 panel DMA buffer、
  driver-owned bounce buffer 或屏幕控制器仍在读取的内存；这些对象必须由 port 在 flush-done 边界管理。

## CPU/指令集建议

- Tokenizer 和 CSS parser 优先保持分支可预测的 ASCII fast paths。
- 在 text shaping 出现前，将 UTF-8 decoding 限定在 entity/codepoint 转换处。
- 目标 ISA 未确定前不做 SIMD；Cortex-M、Cortex-A、RISC-V 和 x86 的收益差异很大。
- Geometry 和 style units 优先使用整数和 fixed-point。

## I/O 建议

- 避免在 render pipeline 中同步 network/font/image decode。
- 资源流入有界 buffer。
- 等 layout 建立可见盒后再延迟 image decode。
- 显示刷新使用 dirty rectangles。
- 显示 buffer 保持由宿主持有；当屏幕使用 RGB565、RGB332、灰度或单色格式时，
  通过 `embedded_framebuffer` 转换。
- 若屏幕驱动只接受 internal DMA buffer，写自定义 `HostFrameSink`，按条带从 RGBA framebuffer 转换并等待
  每段 flush 完成；不要为了使用通用 adapter 而常驻一个 internal 整屏 RGB565 target。

## Release 微基准基线

命令：

```powershell
.\build\Release\jellyframe_render_core_microbench.exe 80 1000
```

加入响应式 grid/aspect-ratio layout 子集后，本 Windows 构建机结果：

```text
html_parse avg_us=990.255
css_parse avg_us=35.898
render_tree avg_us=1054.36
layout avg_us=259.59
layer_tree avg_us=117.187
flatten_layers avg_us=24.529
full_pipeline avg_us=2228.91
```

解读：

- Debug 数据不适合做性能决策。
- Rule indexing 已显著降低 render-tree/style-resolution 成本。
- 固定 cascade slots 移除了每个节点的 cascade hash 表初始化。
- listener 惰性存储避免事件支持增加普通无 listener DOM 节点的常驻内存。
- Layer tree 增加少量显式成本，但为 clipping、ordering 和后续 dirty-layer repaint 准备了结构。
- 更宽的 fallback CSS 和继承文本属性增加了 style/render 工作，但避免了常见现代页面上的灾难性视觉失败。
- wrapped text 额外行高余量避免原生文本后端裁剪，代价是少量 layout 成本。
- 收集内嵌 `<style>` 以及更宽的长度/属性支持改善了常见静态页面，代价是少量 style/render 成本。
- 响应式 grid card 和 `aspect-ratio` 增加了可测量的 layout 工作，但成本有界，
  换来了明显更强的嵌入式应用 UI 表达能力。
- 当前 full pipeline 仍主要由 HTML parse 和 style/render 工作主导。
- 下一项性能升级应转向重复 class pattern 的 computed-style sharing；如果硬件内存压力证明
  full framebuffer 路径过贵，再推进 tile/scanline presentation。
