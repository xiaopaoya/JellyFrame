# 嵌入式优化说明

日期：2026-06-14

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
- DOM event listener storage 惰性分配，没有 listener 的节点不携带空 listener table。
- 平台文本绘制通过可选回调注入；核心 renderer 保留可移植 bitmap fallback，不链接 Win32/GDI。
- software rasterizer 对不透明矩形使用直接行填充。
- offscreen compositing 在像素循环前裁剪 source/destination rectangles。

## 内存建议

- 如果目标栈很小，应替换递归析构/遍历。
- 当对象生命周期绑定到 document 后，为 DOM/render/layout objects 添加 arena allocation。
- 面向真实大型样式表前，按 id/class/tag 建 CSS rule index。
- 在小 RAM 系统上限制 layer/display-list 输出，或按 dirty region 分块生成。

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

## Release 微基准基线

命令：

```powershell
.\build\Release\wearweb_microbench.exe 80 1000
```

加入 event/input、listener 惰性存储、平台文本回调抽取和 software renderer 热路径清理后，本 Windows 构建机结果：

```text
html_parse avg_us=971.673
css_parse avg_us=35.7
render_tree avg_us=1045.13
layout avg_us=211.992
layer_tree avg_us=92.671
flatten_layers avg_us=24.476
full_pipeline avg_us=2126.63
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
- 当前 full pipeline 仍主要由 HTML parse 和 style/render 工作主导。
- 下一项性能升级应是 arena allocation 和针对重复 class pattern 的 computed-style sharing。
