# DOM Arena 可行性

日期：2026-06-16

本文记录当前是否应把 DOM node 移入 arena allocation 的判断。Render tree、layout tree 和
layer tree 已经有 arena-backed 构建路径，因为它们的生命周期基本属于单帧或单次文档管线。
DOM node 更复杂：它会被脚本观察，会在 attached/detached 状态之间移动，并且会在解析后继续
mutation。

## 当前所有权模型

- `HtmlParser::parse()` 返回文档根节点的 `std::unique_ptr<Node>`。
- `Node::children` 用 `std::unique_ptr<Node>` 持有子节点。
- `Node::append_child()` 把所有权移入父节点。
- `Node::detach_child()` 把所有权从父节点移出。
- `Node::remove_child()` 立即销毁 detached 子树。
- JerryScript binding 会把 detached nodes 保存在 `JerryScriptRuntime::detached_nodes_` 中。
- JS `createElement()` 和 `createTextNode()` 会先创建 detached node，然后 `appendChild()` 可以
  把它移入文档。

这个模型很容易推理，也适合 mutation-heavy UI，但每个 DOM node 仍然是一次小堆分配。

## 已经完成的改进

- DOM attributes 已使用紧凑顺序 `AttributeList`，不再为每节点维护 hash table。
- DOM dirty flags 会向上冒泡，因此根节点 clean check 是 O(1)。
- DOM 子树销毁和整子树 `textContent` 替换现在使用显式工作列表，避免极深生成式文档通过
  子节点析构递归压栈。

## 为什么不能立刻做完整 DOM Arena

朴素 document arena 可以让整文档销毁很便宜，但会破坏或显著复杂化当前 mutation 语义：

- `detach_child()` 当前返回所有权。arena-owned node 不能自然地作为普通 `std::unique_ptr<Node>`
  单独返回。
- JS wrapper 可以在节点脱离文档后继续存在。detached node 仍需要稳定 owner。
- JS `removeChild()` 会返回仍可继续使用的 node。立即 arena 回收是不正确的。
- Event listener、form-control state 和 script wrapper reference 都会把可变状态挂到 node 上。
- Parser-created node 和 script-created node 目前共用同一种类型和所有权路径，这让 API 复杂度很低。

## 安全路线

1. 对 scripting-enabled document 保留当前 heap-owned mutable DOM。
2. 先补测量：node count、平均 child 数、平均 attribute 数，以及 detached-node 数量。
3. 只有在能同时管理 root 和 detached nodes 的生命周期边界时，再引入 `Document` 或 `DomOwner`
   wrapper。
4. 可以考虑 parser-only arena mode，用于不暴露 `detach_child()` 和 script-created nodes 的静态文档。
   该模式必须显式开启，因为它的 mutation 语义更弱。
5. 如果仍然需要完整 mutable DOM arena，应使用自定义 node handle/deleter，而不是直接继续用裸
   `std::unique_ptr<Node>`，并一次性有控制地更新所有 mutation API。

## 当前建议

现在不应直接把 DOM nodes 转为 arena allocation。当前更好的权衡是：

- 保持 DOM 所有权简单且正确；
- 保持 render/layout/layer arena-backed；
- 继续渐进移除 DOM 的栈风险和 per-node 开销；
- 在引入更大所有权抽象前，先拿到真实嵌入式测量结果。

下一项低风险 DOM 工作应是 instrumentation 或 `DomOwner` 设计原型，而不是直接替换 allocator。
