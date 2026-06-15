# 内存管理审视

WearWeb 当前优先采用明确所有权和小型、可预测容器。这是嵌入式目标的正确起点，
但仍有一些分配方式更偏桌面便利，而不是极限 MCU 友好。

## 当前已经合适的部分

- DOM、render tree、layout tree 和 layer tree 对象使用 `std::unique_ptr`，所有权路径单一，
  销毁确定。
- 核心构建不会链接 JerryScript、Win32、文件 I/O loader 或平台字体代码，除非可选 target 明确启用。
- Event listener 存储是惰性的：没有 listener 的节点不会分配 listener table。
- 表单控件状态是惰性的：普通元素不会携带控件状态。
- Parser 和示例文件输入都有上限。
- Timer callback 被显式保留为 JerryScript reference，并在 `clearTimeout`、`clearInterval`、
  重新绑定文档和 runtime 销毁时释放。
- M6 timer queue 由宿主泵动，并带有 callback budget，因此大量到期 timer 不会无限占用单帧。

## 嵌入式风险

- DOM attributes 使用 `std::unordered_map`，写起来简单，但对小元素偏重。多数嵌入式 UI 节点只有很少属性。
- DOM/render/layout/layer tree 会独立分配许多小对象。这样清晰安全，但可能造成小堆碎片。
- 多处树操作是递归的。极小栈目标可能需要把 parsing、dirty flag 扫描和销毁路径改成迭代遍历。
- Framebuffer 内存和 viewport 面积线性相关。390x640 RGBA buffer 在任何离屏 layer 之前就约 1 MiB。
- 嵌入式 presentation 可以用 `embedded_framebuffer` 把 dirty rectangles 转换到宿主持有的
  RGB565、灰度或单色 buffer，从而避免再保留一份 RGBA 大小的显示 buffer。
- opacity 或 transform layer 的离屏合成可能分配临时 framebuffer。
- 文本使用 `std::string` 存储；未来文本 shaping 或大页面需要更严格的字符串生命周期和去重策略。
- 脚本 wrapper 按需短生命周期创建。这样避免陈旧所有权，但脚本热路径可能产生额外内存抖动。

## 建议的下一步优化

1. 把每节点 attribute hash map 替换成 small-vector attribute list，然后按需给 `id` 和 `class` 加微型索引。
2. 为文档生命周期对象加入可选 arena：DOM node、render object、layout box 和 layer node。
3. 为嵌入式目标保留明确 framebuffer 策略：一个主 framebuffer、可选 dirty rectangle、
   必要时一份宿主持有的转换后显示 buffer，并严格限制离屏 buffer。
4. 当目标栈预算很小时，把递归树遍历改成迭代。
5. 增加宿主可配置预算：最大 DOM 节点数、CSS 规则数、display command 数、timer 数和 JS event listener 数。
6. 只有在测量脚本密集应用后再考虑 wrapper cache；它能减少重复 wrapper 创建，但会保留更多 JerryScript 对象。

## 当前判断

项目现在足以继续 M6/M7 开发，但还不是极小 MCU 的最优堆模型。未来收益最大的优化是 document arena
和 small-vector attributes；二者都能保持可读性，同时移除大量小堆分配。
