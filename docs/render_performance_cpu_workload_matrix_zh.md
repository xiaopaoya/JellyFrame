# Render Core CPU 对照 workload 矩阵

> 最后更新：2026-09-17；适用版本：0.6.0-dev
> 状态：Stage 3 桌面对照定义稿；不是硬件验收结果

本文冻结 Render Core 在桌面 CPU 上与其他 CPU 2D backend 对照时可以接受的
workload 边界。目的不是给 JellyFrame、Cairo、SDL、LVGL 或浏览器排总体名次，
而是保证每个数字只回答一个可复核的问题。

## 1. 共同条件

每个 adapter 都必须输出 `jellyframe.benchmark.run.v0`，并且以下字段完全相同：

- viewport、像素格式、alpha 语义和 full/dirty repaint mode；
- 抗锯齿约定、颜色空间、blend 模式和裁剪区域；
- workload 版本、输入数据、字体资源和字体 fallback 策略；
- warm-up 次数、样本数、进程/线程模型、构建类型和机器信息；
- 输出校验方法、参考 fixture 和容差。

输出校验失败、字体身份不一致或任一固定条件不同，比较器必须返回
`not-comparable`，不能继续计算 MPix/s 或把较快的一侧称为胜出。

## 2. 矩阵

| workload | 当前状态 | 固定内容 | 可回答的问题 |
| --- | --- | --- | --- |
| `opaque-fill-rgb-v1` | 已接受 | 172x320、RGB888、全屏不透明填充、无 AA、exact RGB | 纯矩形写入和像素吞吐 |
| `horizontal-gradient-rgb-v1` | 已接受 | 同上、横向不透明线性渐变、无 AA、RMSE <= 1 | 线性渐变的每像素计算成本 |
| `vertical-gradient-rgb-v1` | 已接受 | 同上、纵向不透明线性渐变、无 AA、RMSE <= 1 | 另一种渐变访问方向的成本 |
| `rounded-card-rgb-v1` | Core probe 已有；跨库待适配 | 172x320 黑底、固定卡片矩形/四角半径/颜色、明确 AA 和 source-over | 圆角 coverage、边框和裁剪成本 |
| `text-lines-v1` | Core probe 已有；跨库待适配 | 固定字体包、family/hash、字号、weight、文本、宽度和换行模式 | 文本测量、换行和绘制成本 |
| `embedded-ui-v1` | fixture 已资格通过；设备矩阵待执行 | 黑底可穿戴页面、设置行、状态卡、导航、文本更新、滚动、全屏重绘 | 真实设备端到端阶段成本 |

前三项由 Windows `jellyframe_cpu2d_compare` 和 memory-DIB GDI 适配器提供。
`rounded-card-rgb-v1` 不应直接使用没有抗锯齿的 `GDI RoundRect` 作为等价参考：
Render Core 的圆角边缘采用 4x4 coverage，两个结果的边缘像素语义不同。未来
适配器必须使用同等 coverage 规则，或者把两边都固定到明确的无 AA 几何模式，
并在 manifest 中声明该选择。

`text-lines-v1` 不能用“Render Core 内置 bitmap fallback 对 Windows GDI 默认字体”
作为公平比较。文本结果受字体包、字号 fallback、hinting、字形 shaping、换行和
宿主 `TextPainter` 影响。只有以下两种方式可以进入 comparable：

1. 两个 backend 使用同一字体文件、同一 glyph rasterizer 和同一 shaping/测量适配器，
   只比较外层命令和缓冲区处理；
2. 双方输出经过固定的文本 fixture 校验，并把 `fontIdentity`、`fontSize`、
   `fontWeight`、`wrapWidth`、`lineBreakMode` 和 AA 规则写入 workload 条件。

否则只能把结果作为 JellyFrame 自身的 `text_*` microbench baseline，不能进入跨库
性能报告。

## 3. JellyFrame 侧 probe 对应

不需要硬件即可运行的 Core probe 当前包括：

- 圆角：`rounded_rect_aa_raster`、rounded clip/composite 统计和
  `soft_box_shadow_raster`；shadow 不得冒充 rounded-card 结果，二者必须分开报告；
- 文本：`text_plain_layout`、`text_plain_layer`、
  `text_spacing_anywhere_layout`、`text_spacing_anywhere_layer`、
  `text_anywhere_wrap_32/128/512/2048` 及其 `*_wide_*` 变体；
- dirty 文本：`dirty_text_clip_transient_surface` 和
  `dirty_text_clip_reused_surface`；
- 设备映射：`embedded-ui-v1` 的 `static-local-repaint`、
  `text-layout-update`、`drag-scroll`、`full-repaint`。

这些 probe 的输出是 Core 内部趋势和回归基线。它们不自动等于 library comparison，
也不把布局、paint、present、DMA 的时间相加。

## 4. 适配器最小要求

新增 Cairo、SDL、LVGL 或其他 adapter 时，至少提交：

1. workload 输入和固定条件说明；
2. 运行输出 manifest，包含 `outputValidation` 和 `measurements`；
3. 与 JellyFrame 相同的 warm-up、样本数和 release/debug 条件；
4. 输出校验失败、缺字体、缺 AA 或缺 dirty 支持时的显式状态；
5. 一项自动化 manifest/比较器回归测试。

适配器不能在 manifest 中把不支持的能力伪装成已支持。例如 backend 没有 dirty
repaint 时，不能把 full repaint 的结果标成 `mode: dirty`；字体不一致时，不能只
省略字体字段来绕过固定条件检查。

## 5. 当前阶段结论

现阶段可以对外引用的桌面对照仅限三个已接受的矩形 primitive。圆角和文本已经
有可重复的 JellyFrame 内部 probe，但跨库适配仍是待完成工作；嵌入式 UI 则须在
ESP32-S3 上按 [硬件对照要求](render_performance_hardware_comparison_zh.md)
完成 baseline/candidate 矩阵后再给出设备结论。

这一边界比给出覆盖条件不一致的“总体快慢”数字更严格，也更适合定位下一项
Render Core 优化：先用内部 probe 判断算法变化，再用固定适配器或真实设备确认
端到端收益。
