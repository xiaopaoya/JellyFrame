# Render Trace 命令/节点归因 RFC

> 最后更新：2026-09-10；适用版本：0.6.0-dev  
> 状态：设计冻结；尚未启用 producer

## 目标

为显式 Render Trace profiling 提供可信的“哪类绘制命令、由哪个元素产生、覆盖何处、耗时多久”归因，
用于定位 App 的桌面壳渲染瓶颈。它不是浏览器 DevTools 的兼容承诺，也不是设备 FPS、DMA 或 panel
计时替代品。

该能力必须同时适用于一个 layer 内混合多个 DOM box 的情形；因此不能把 layer 的单个 `box` 误当作
其中所有 `DisplayCommand` 的来源。

## 不变量

- `DisplayCommand`、value-frame、script task frame 和 JFDP payload 继续是纯值；绝不写入 `Node*`、
  `LayoutBox*`、`LayerNode*`、地址、内存布局或文件系统路径。
- 默认构建和正常渲染不注册时钟、不保存 command owner、不产生额外字符串或每命令哈希表查找。
  所有额外工作只由明确的 desktop profiling/trace 配置开启。
- trace 中最多导出 64 个 command 聚合项、64 个 node 描述和 4 KiB 单行；超限必须用明确的
  `commandsTruncated` / `nodesTruncated` 标记，绝不能无声遗漏并假称完整归因。
- 归因记录的是实际执行的 raster work，不能把整个 layer 或 paint 阶段时间猜分给第一个元素。
  没有可靠 owner 或无有效时钟样本时保留 `unattributed`，而不是伪造 `nodeId`。
- 正确性优先：开启或关闭 profiling 的 framebuffer 像素 hash 必须一致。profiling 开销单独报告，
  不把 profiling 帧直接用于性能优化结论。

## 数据模型

### Core 内部 owner token

`DisplayCommand` 增加仅供诊断的 32-bit `trace_owner_token`，默认 `0`（`unattributed`）。它是本次
layer-tree build 内的短生命周期 opaque value：可以复制、flatten 和跨 task 编码时忽略，但不代表
持久 DOM identity，也不进入 App API 或包格式。

当 `LayerTreeBuilderOptions::trace_owner_registry` 明确提供时，builder 在每个 `LayoutBox` 的
`paint_box_self` / `::before` / `::after` 命令范围完成后，才将 registry 返回的 token 写入新命令。
未启用 registry 时不写 token，也不创建 owner sidecar。select popup、scroll indicator 等宿主/临时
overlay 必须分别标为控件 owner 或 `unattributed-overlay`，不能借用邻近元素。

registry 只在当前 build 中维护 `Node* -> token` 的内部映射；其生成的 trace 表可在同一帧结束前把
token 解析为以下一个受限描述：

1. 元素具有唯一非空 ASCII `id` 时：`kind: "id"`，`value` 为该 `id`；
2. 否则：`kind: "opaque"`，`value` 为本 trace session 单调产生的 `n1`、`n2` 等 token；
3. 没有 DOM owner 的宿主 overlay：`kind: "overlay"`，不附带 DOM 路径。

不导出 `dom_node_path()`、tag/class/文本、CSS selector、指针或源文件位置。这样既不把临时结构误作
稳定 API，也避免 trace 因用户文本或结构细节无界膨胀。

### Trace V0 增量

frame record 的可选 `commands` 数组按 `(ownerToken, type)` 聚合：

```json
"commands":[
  {"type":"BoxShadow","owner":"id:card-1","us":7200,"pixels":3820,"samples":1},
  {"type":"Text","owner":"n7","us":610,"pixels":340,"samples":2},
  {"type":"FillRect","owner":"unattributed-overlay","us":90,"pixels":160,"samples":2}
]
```

- `us` 是被 profiling clock 包围的实际 raster invocation 总和；`samples` 是合并前 invocation 数；
  `pixels` 是命令 rect 与当前 paint clip 的保守交集像素数，不等于可见/不透明像素。
- `owner` 是上述受限 token，不保证跨 frame、跨 reload 或跨设备稳定；拥有 DOM `id` 的条目才可跨帧
  对比。旧 viewer 保持将它作为 `nodeId`/文本列展示的兼容读取。
- `commandTimingComplete` 默认 `false`。rounded temporary surface 的 prepare/composite、图层 transform
  和 host text/image callback 的内部工作若不能精确拆分，必须留下未归因间隙。
- 每条 frame 还可带 `commandAttributionOverheadUs`，用于标记 profiling 自身代价；没有可靠 clock 时省略。

## 实现顺序

1. **Owner token sidecar**：添加 opt-in registry、命令范围 stamping、flatten 保留和单元测试；验证
   普通 `DisplayList`/frame codec 的像素与字节结果完全不变。
2. **桌面命令计时**：为 `SoftwareRasterizer` 增加可选 observer，在每个真实 invocation 前后读取
   host clock；rounded grouped replay 仅在能够归属每个 command 时输出细分，否则单独记为
   `unattributed-rounded-composite`。
3. **有界聚合与 UI**：Win32 producer 聚合到 bounded JSONL，查看器显示 type/owner/pixel/sample 排名，
   并清楚显示 timing incomplete、截断和 profiling overhead。
4. **正确性及开销门槛**：同一 `.jfcapture` 的 profile on/off frame hash 必须相同；Release desktop
   baseline 上 profiling p95 额外 CPU 时间应记录且可解释，不设虚假的“零开销”要求。
5. **设备 profile（后续）**：只在独立 Kconfig/profile 开启，先输出 type 聚合和窗口汇总；除非端侧时钟
   与缓冲预算通过实机验收，不输出逐元素时间。

## 必测场景与出口

- 一个普通 layer 包含多个有/无 `id` 的 box、文本多行、`::before`/`::after`、边框和背景；每个命令
  只能归于正确 box 或明确 `unattributed`。
- 具有 opacity、transform、overflow clip、rounded clip、scroll indicator 和 select popup 的 layers；
  token 不能因 flatten/reorder 错配。
- `max_display_commands`、trace 行上限、聚合项上限和 malformed JSONL；全部必须明确截断/拒绝。
- full repaint 与 dirty repaint、profiling on/off 的 capture 必须逐帧像素相等；不开启 profiling 的
  Core benchmark 不得出现回归。
- trace 中不出现裸地址、DOM path、用户文本、文件路径、密钥或设备物理地址。

达到以上门槛后，才可以把 Render Trace 的“可用命令归因”升级为“已交付 command/node attribution”。
在此之前，面板维持“无 command/node attribution”的诚实状态。
