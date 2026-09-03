# Render Core 预算契约 RFC 草案

> 状态：草案，不改变当前 ABI/API。
> 日期：2026-09-03
> 范围：dirty region、display invalidation、clip chain 和 display command 输入。

## 1. 背景与目标

当前 Core 的若干公共入口同时接受指针和 `std::size_t` 计数。Runtime 的 value-frame
codec 已经在进入 Core 前执行 `max_commands`、`max_clips`、`max_dirty_rects` 等限制，
但 Core 自身没有统一的预算对象和结果语义。这样会产生两个维护风险：

- 新的 consumer 可能忘记在边界层限制计数，导致 `reserve(count)` 或二次遍历成本失控；
- 超限时若直接截断，结果可能看起来像有效帧，却丢失必要的 clip 或 dirty 区域，造成
  画面错误且难以归因。

目标是让每个入口都能明确回答：输入是否完整有效、消耗了多少预算、失败是否可以安全
重试；不在 Core 内引入异常、全局状态或运行时分配器依赖。

## 2. 非目标

- 本 RFC 不授权 retained replay、framebuffer reuse、dirty rendering 或 tile renderer。
- 不改变现有 `0` 表示“不限制”的兼容语义，直到所有 consumer 完成迁移。
- 不为追求统一而机械重命名现有字段；迁移时只替换语义不准确的公开名称。
- 不把设备/Runtime 的协议字段直接暴露给 Render Core。

## 3. 现有实际边界

| 输入 | 当前限制来源 | Core 当前风险 |
| --- | --- | --- |
| display commands | value-frame codec 的 `max_commands`、layer builder 的 display budget | 直接构造 `DisplayList` 的 consumer 可绕过预算 |
| clip records | codec 的 `max_clips`，索引另有 `uint16_t` 上限 | 直接 rasterizer 调用仍可提供很大的 `clip_count` |
| dirty rectangles | `HostBudgets.max_dirty_rects`、`FrameScratch` | `analyze_display_invalidation()` 会按传入计数 reserve，并可能做 O(n²) 包含判断 |
| nested clip visits | `max_clip_depth` 与 frame/profile budget | 入口没有统一的“预算拒绝”结果 |
| framebuffer/temporary pixels | rasterizer/compositor 各自的 pixel budget | 预算类型和错误诊断分散在不同层 |

上述风险目前由调用方预算和测试约束住，不能据此宣称 Core 公共 API 已具备统一的
资源边界。

## 4. 建议的抽象

在下一次 ABI 变更或明确的 Core minor release window 中，引入只读的入口预算：

```cpp
struct RenderCoreBudget {
    std::size_t max_display_commands = 0;
    std::size_t max_clip_records = 0;
    std::size_t max_dirty_rects = 0;
    std::size_t max_clip_depth = 0;
    std::size_t max_temporary_pixels = 0;
};

enum class RenderCoreBudgetStatus {
    Accepted,
    NullInput,
    CountExceeded,
    InvalidRelationship,
    ArithmeticOverflow,
};

struct RenderCoreBudgetReport {
    RenderCoreBudgetStatus status = RenderCoreBudgetStatus::Accepted;
    std::size_t accepted_count = 0;
    std::size_t estimated_work = 0;
};
```

具体命名和 ABI 形态待 API review 决定。关键约束是：

1. `0` 继续表示无限制，但生产 consumer 必须提供非零 profile budget；Core-only 工具
   可以明确选择 unlimited。
2. 入口先验证 `pointer/count` 关系，再做任何 `reserve`、乘法或递归遍历。
3. 超限返回 `CountExceeded`，不静默截断，不部分应用输入。
4. `estimated_work` 只用于诊断和 benchmark，不作为安全检查的唯一依据；所有容量和
   字节计算仍必须使用 checked arithmetic。
5. 结果对象不携带平台错误码。Runtime/port 将 Core status 映射为自己的 typed reason。

## 5. 入口迁移规则

### 5.1 Dirty 和 invalidation

`analyze_display_invalidation()` 应迁移为接收一个 span 加预算的 view。函数在归一化前
拒绝超过 `max_dirty_rects` 的输入；归一化后的数量不能超过同一预算。当前 O(n²) 的
包含关系判断先保持不变，只有 benchmark 证明它成为真实 workload 瓶颈后才引入空间索引。

### 5.2 Clip chain

`rasterize_clipped()` 应将 command、clip chain 和 target budget 作为同一调用上下文验证。
任何 clip index 越界、空指针/非零计数或 clip depth 超限均返回稳定的拒绝状态；不得
绘制部分 command 后再报告失败。现有 void overload 在迁移期保留为内部兼容包装器，
新 consumer 不应继续使用。

### 5.3 Display command

`DisplayList` 的产生者负责在构建时限制 command 数；消费方仍必须检查自身预算，不能
因为 `std::vector` 已经存在就认为输入可信。command 与 clip index 的对应关系应在
一次 validation pass 中检查完毕，再进入 paint pass。

## 6. 调用方责任

- Runtime codec：继续执行 wire payload 字节预算和协议关系校验，将 codec 上限传给 Core。
- Desktop shell：使用 profile 文件生成确定的 Core budget，不以“桌面内存足够”为理由
  放宽 malformed input 路径。
- Device OS/port：按设备 framebuffer、PSRAM 和任务时限提供 profile budget，并将
  `CountExceeded`、`ArithmeticOverflow` 映射为可定位的日志字段。
- Render Core：不读取环境变量、不访问文件系统、不推断设备型号；只执行传入预算。

## 7. 必须先完成的证据

进入实现前必须具备：

1. Runtime、桌面壳、嵌入式 framebuffer 和 value-frame renderer 的调用点清单；
2. `300x300`、`320x240`、`172x320` 三种 profile 的实际 budget 表；
3. null pointer、`SIZE_MAX` count、关系不一致、乘法溢出和预算边界的正负测试；
4. 迁移前后正常 frame 的像素等效测试；
5. dirty normalization、clip coverage 和 display validation 的 focused benchmark；
6. Core standalone、package consumer、source override 和至少一个设备 profile CI。

未满足这些条件前，保持现有调用方预算，不修改公共签名。

## 8. 当前决定

本轮只归档 RFC，不实现 `RenderCoreBudget`。当前 R1 的结论仍是：现有调用方约束足以
通过已验证测试，但统一 Core 公共预算契约尚未完成，不能把它描述为已解决的安全边界。

