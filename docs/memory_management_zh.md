# 内存管理审视

JellyFrame 当前优先采用明确所有权和小型、可预测容器。这是嵌入式目标的正确起点，
但仍有一些分配方式更偏桌面便利，而不是极限 MCU 友好。

## 当前已经合适的部分

- DOM、render tree、layout tree 和 layer tree 对象使用 `std::unique_ptr`，所有权路径单一，
  销毁确定。
- 核心构建不会链接 JerryScript、Win32、文件 I/O loader 或平台字体代码，除非可选 target 明确启用。
- Event listener 存储是惰性的：没有 listener 的节点不会分配 listener table。
- 表单控件状态是惰性的：普通元素不会携带控件状态。
- Parser 和示例文件输入都有上限。
- `HostBudgets` 现在会映射到 HTML parser、CSS parser、render tree、layout tree、
  layer tree、flatten 后的 display-list、dirty rectangles、JerryScript timer 和脚本事件
  listener 限制。
- Timer callback 被显式保留为 JerryScript reference，并在 `clearTimeout`、`clearInterval`、
  重新绑定文档和 runtime 销毁时释放。
- M6 timer queue 由宿主泵动，并带有 callback budget，因此大量到期 timer 不会无限占用单帧。
- `MonotonicArena` 已作为核心内存工具加入，支持块式线性分配、反序析构和整 arena reset。
  Render tree、layout tree 和 layer tree 已提供 arena 构建入口，并在 microbench、virtual board
  和 ESP32-S3 benchmark 中启用；DOM node 仍按原有所有权模型运行。

## 嵌入式风险

- DOM attributes 已从每节点 `std::unordered_map` 改为紧凑顺序 `AttributeList`。多数嵌入式 UI
  节点只有少量属性，线性扫描比维护 hash buckets 更省内存且更可预测。
- DOM node 仍会独立分配许多小对象。这样清晰安全，但可能造成小堆碎片。
- 多处树操作是递归的。极小栈目标可能需要把 parsing、dirty flag 扫描和销毁路径改成迭代遍历。
- Framebuffer 内存和 viewport 面积线性相关。390x640 RGBA buffer 在任何离屏 layer 之前就约 1 MiB。
- 嵌入式 presentation 可以用 `embedded_framebuffer` 把 dirty rectangles 转换到宿主持有的
  RGB565、灰度或单色 buffer，从而避免再保留一份 RGBA 大小的显示 buffer。
- opacity 或 transform layer 的离屏合成可能分配临时 framebuffer；这条路径仍需要严格宿主预算。
- 文本使用 `std::string` 存储；未来文本 shaping 或大页面需要更严格的字符串生命周期和去重策略。
- `StyleResolver` 持有有界候选规则缓存。它用少量、可配置的 resolver 私有内存换取 class-heavy UI
  树中更少的重复 bucket 合并；最终 cascade 结果仍逐节点重新计算。
- 脚本 wrapper 按需短生命周期创建。这样避免陈旧所有权，但脚本热路径可能产生额外内存抖动。

## 建议的下一步优化

1. 评估 DOM node 是否应进入 document arena。这比 render/layout/layer 风险更高，因为 parser、
   mutation 和 scripting 都会观察 node 所有权。
2. 为嵌入式目标保留明确 framebuffer 策略：一个主 framebuffer、可选 dirty rectangle、
   必要时一份宿主持有的转换后显示 buffer，并严格限制离屏 buffer。
3. 当目标栈预算很小时，把递归树遍历改成迭代。
4. 把预算继续贯穿到剩余高成本路径：离屏合成、资源聚合，以及未来 image/font decoder。
5. 在真实应用上测量候选规则缓存后，再考虑完整 computed-style sharing；完整共享必须处理继承值和
   mutation invalidation。
6. 只有在测量脚本密集应用后再考虑 wrapper cache；它能减少重复 wrapper 创建，但会保留更多 JerryScript 对象。

## 当前判断

主要管线阶段已经消费 `HostBudgets`，DOM attributes 也已经去掉每节点 hash map。项目现在更适合做
有界嵌入式 bring-up。它仍不是极小 MCU 的最优堆模型；render/layout/layer tree 都已经有
arena 构建路径。剩余最大的分配问题是：能否把 DOM node 移入 document arena，同时不破坏
mutation/script 的可读性和安全性。
