# Render Core CPU 对照 workload 矩阵

> 最后更新：2026-09-19；适用版本：0.6.0-dev
> 状态：Stage 3 桌面对照定义稿；不是硬件验收结果

本文冻结 Render Core 在桌面 CPU 上与其他 CPU 2D backend 对照时可以接受的
workload 边界。目的不是给 JellyFrame、Cairo、SDL、LVGL 或浏览器排总体名次，
而是保证每个数字只回答一个可复核的问题。

## 1. 共同条件

每个 adapter 都必须输出 `jellyframe.benchmark.run.v0`，并且以下字段完全相同：

- viewport、像素格式、alpha 语义和 full/dirty repaint mode；
- 抗锯齿约定、颜色空间、blend 模式和裁剪区域；
- workload 版本、输入数据、字体资源和字体 fallback 策略；
- `workloadParameters` 中的几何、颜色、blend、轴向和端点约定；
- warm-up 次数、样本数、进程/线程模型、构建类型和机器信息；
- 输出校验方法、参考 fixture 和容差。

输出校验失败、字体身份不一致或任一固定条件不同，比较器必须返回
`not-comparable`，不能继续计算 MPix/s 或把较快的一侧称为胜出。
新 adapter 必须输出有界的 `workloadParameters` 对象；旧 manifest 仅在两侧都缺少该字段时
保持兼容，新旧混用不得比较。

## 2. 矩阵

| workload | 当前状态 | 固定内容 | 可回答的问题 |
| --- | --- | --- | --- |
| `opaque-fill-rgb-v1` | 已接受；GDI/SDL2 adapter | 172x320、RGB888、全屏不透明填充、无 AA、exact RGB | 纯矩形写入和像素吞吐 |
| `opaque-dirty-fill-rgb-v1` | 已接受；GDI/SDL2 adapter | 172x320 黑底、固定 96x48 脏矩形、RGB888、无 AA、exact RGB、每样本 64 次 | 局部不透明写入和 dirty 像素吞吐 |
| `alpha-grid-rgb-v2` | 已接受；GDI/SDL2 adapter | 172x320 黑底、8x8 个 16x12 不重叠矩形、RGBA `50b4dc80`、source-over、exact RGB、计时内完成同步 | 半透明 source-over 的批平均单块成本 |
| `horizontal-gradient-rgb-v1` | 已接受 | 同上、横向不透明线性渐变、无 AA、RMSE <= 1 | 线性渐变的每像素计算成本 |
| `vertical-gradient-rgb-v1` | 已接受 | 同上、纵向不透明线性渐变、无 AA、RMSE <= 1 | 另一种渐变访问方向的成本 |
| `rounded-card-rgb-v1` | Core probe 已有；GDI/GDI+ 超采样资格检查不等价，AA adapter 待适配 | 172x320 黑底、固定卡片矩形/四角半径/颜色、明确 AA 和 source-over | 圆角 coverage、边框和裁剪成本 |
| `bitmap-clock-text-rgb-v1` | 已接受；GDI bitmap-glyph blit adapter | 共享 clock-5x7-v1 字形，2x 最近邻缩放，8 行固定 ASCII 时间串，黑底，exact RGB | 固定 bitmap 文本的命令与字形绘制成本，不包括系统字体/shaping/自动换行 |
| `text-lines-v1` | Core probe 已有；跨库待适配 | 固定字体包、family/hash、字号、weight、文本、宽度和换行模式 | 文本测量、换行和绘制成本 |
| `embedded-ui-v1` | fixture 及两项候选的四 workload 非回归矩阵已通过 | 黑底可穿戴页面、设置行、状态卡、导航、文本更新、滚动、全屏重绘 | 已测 repaint/aggregate 阶段成本，不代表完整脚本/布局路径 |

五项矩形 workload 由 Windows `jellyframe_cpu2d_compare` 和 memory-DIB GDI 适配器提供；其中
`opaque-fill-rgb-v1`、`opaque-dirty-fill-rgb-v1` 与 `alpha-grid-rgb-v2` 还可通过运行时加载的 SDL2 DLL 使用
`SDL_CreateSoftwareRenderer` 对照。SDL2 adapter 不进入 Runtime/SDK/App 依赖，并且会拒绝
没有等价 SDL primitive 的渐变 workload。

局部填充的 64 次批处理只用于降低亚微秒单次操作的计时噪声；manifest 记录
`operationsPerSample: 64`，比较器会拒绝批处理数量不同的结果。报告中的 `paint_us` 已除以 64，
`pixels` 仍是单次操作实际覆盖的 4,608 像素，不得乘成整帧面积。该 workload 不包含 dirty region
生成、layout、窗口 present 或设备 DMA。

`alpha-grid-rgb-v2` 每个样本在计时外恢复黑底并同步，再在计时内绘制 64 个不重叠 tile 并完成队列同步，避免重复
source-over 改变后续操作的输入。`paint_us` 是单 tile 平均值，`pixels` 是单 tile 的 192；
JellyFrame、GDI `AlphaBlend` 和 SDL2 software blend 必须达到 exact normalized RGB。
逐样本交错交换先后顺序，最终输出另经独立像素公式验证。V1 缺少完成同步，旧排名已撤回，
不得与 V2 混比；批平均的 p95 不代表单个 tile 的尾延迟。

`rounded-card-rgb-v1` 不应直接使用没有抗锯齿的 `GDI RoundRect` 作为等价参考：
Render Core 的圆角边缘采用 4x4 coverage，两个结果的边缘像素语义不同。未来
适配器必须使用同等 coverage 规则，或者把两边都固定到明确的无 AA 几何模式，
并在 manifest 中声明该选择。

2026-09-19 已新增 `rounded-qualification` 自动化检查：172x320 黑底，卡片
`(14,20,144,96)`、半径 24、RGB `164757`。Core 通过独立距离公式的 4x4
采样 oracle；本机原生 GDI 输出有 392 个不同像素，Core 有 137 个部分覆盖像素。
输出为单独的 `jellyframe.benchmark.qualification.v0`，包含两侧 BMP、差异图、
digest 和误差，固定标记 `not-comparable` / `performanceMeasured: false`。
退出码 0 只表示资格检查执行成功，不表示可比；比较器拒绝把它当作性能 manifest。
本机证据位于 `D:/JellyFramePerf/rounded-qualification-20260919/`，不要求硬件照片逐像素比对。
下一步仍是选择支持等价 AA 的 backend，固定 coverage 契约并通过输出校验后才开始计时；
不能用原生 GDI 二值圆角的速度补齐此项。

后续 `rounded-supersample-qualification` 已测试 GDI+ `FillPath` 的 4 倍分辨率
二值 mask（`SmoothingModeNone`、`PixelOffsetModeNone`）及 4x4 box reduction。
8 个用例包含普通卡片、无圆角对照、小半径、奇数尺寸、最大半径、左侧裁剪、右下裁剪和
2x2 小形状。Core 均通过独立圆方程 oracle；本机 GDI+ 只有无圆角对照完全一致，
其余 7 项不满足 exact coverage。标准卡片差 1 个像素的覆盖率，最大半径差 57 个，
最大覆盖率误差 48/255；不能仅凭标准卡片接近相同就通过适配器。
这里直接比较颜色量化前的 coverage，避免低对比颜色掩盖差异。报告、分例截图和差异图在
`D:/JellyFramePerf/rounded-supersample-qualification-20260919/`，不包含耗时。
这些是本机资格检查结果，不把差异数量固定为跨 Windows 版本的 API 承诺。

圆角后续工作明确区分两条路线：

- **相同 coverage**：继续要求本节的 exact 输出。若通过适配器实现，计时必须包含实际
  绘制、采样归一化及完成同步，不得以预生成 mask 对照 Core 的实时 coverage 计算。
- **不同原生 AA 的质量/成本**：属于新的 workload 合同，尚未放行。须先独立制定几何
  内外部、边缘误差和裁剪的验收标准，再做质量与耗时的联合报告；不能看到本轮误差后
  放宽现有 exact 门槛，也不能只用全屏 RMSE 掩盖稀疏边缘缺陷。

下一步先定义第二条路线的独立质量合同和负例门禁，不再仅通过开启某个 AA 开关认定等价。
该合同完成前，两个被拒绝的适配器均不得产生跨库速度排名，也不需要新增硬件 A/B。

`text-lines-v1` 不能用“Render Core 内置 bitmap fallback 对 Windows GDI 默认字体”
作为公平比较。文本结果受字体包、字号 fallback、hinting、字形 shaping、换行和
宿主 `TextPainter` 影响。只有以下两种方式可以进入 comparable：

1. 两个 backend 使用同一字体文件、同一 glyph rasterizer 和同一 shaping/测量适配器，
   只比较外层命令和缓冲区处理；
2. 双方输出经过固定的文本 fixture 校验，并把 `fontIdentity`、`fontSize`、
   `fontWeight`、`wrapWidth`、`lineBreakMode` 和 AA 规则写入 workload 条件。

否则只能把结果作为 JellyFrame 自身的 `text_*` microbench baseline，不能进入跨库
性能报告。

`bitmap-clock-text-rgb-v1` 是独立的窄范围例外，不表示 `text-lines-v1` 已完成。
JellyFrame 使用现有 `BitmapFont` painter，GDI 使用预建字形图集逐字 `TransparentBlt`；
双方共享六个固定 1bpp glyph、advance 测量和 2x 最近邻缩放，禁止字体 fallback。
manifest 固定字形位图、字号/weight、line height、宽度、两种文本、对齐和行位置。
每样本 8 行，计时内包括 advance 测量、绘制及 GDI 完成同步；字体/图集创建与黑底 reset
在计时外。报告为批平均单行成本，`pixels=548` 是平均每行实际着色像素，不是矩形面积。
全屏输出同时通过独立 mask oracle 和 exact RGB 对比；不会把相同空白图当作通过。
该结果不能评价 GDI `DrawText`、TTF rasterizer、中文 shaping、hinting、AA 或换行；
SDL2 暂无该 adapter，会明确拒绝。缓存图集每字调用的开销是本适配路径的一部分，
不应将其归纳为 GDI 原生文本引擎性能。

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

现阶段可以对外引用的桌面对照限于五个已接受的矩形 primitive 与固定 bitmap 文本子集。
SDL2 覆盖全屏/局部不透明填充与半透明 source-over，GDI 另覆盖两种轴向渐变和 bitmap-glyph blit。
圆角和通用文本已经
有可重复的 JellyFrame 内部 probe，但跨库适配仍是待完成工作；嵌入式 UI 则须在
ESP32-S3 上按 [硬件对照要求](render_performance_hardware_comparison_zh.md)
完成精确 baseline/candidate 矩阵后才可给出对应设备结论。文本缓存 pair 与 source-over
pair 均已通过四 workload 非回归及用户目检，未证实显著设备加速；设备侧仍缺少 script_us，
且这些窗口 pipeline_frames=0，不把该结果扩展为完整 UI 管线验收。

## 6. 多轮复算

`tools/benchmark_compare.py` 的 `--baseline` / `--candidate` 已支持每侧 3–32 个
run manifest，按输入顺序逐轮配对；单文件调用保持原有格式。示例见
[基准工具说明](../benchmarks/README.md#repeated-comparisons)。JSON/HTML 保留每轮 p95、
各轮 p95 的中位数与范围、逐轮变化及输入 SHA-256，不合并原始样本，也不自动宣布显著加速。
所有轮次的条件必须一致；不能靠每对两侧同时改变 viewport/配置绕过检查。
2026-09-19 已使用 source-over 六轮存档复算，得到同样的 -16.88% / -14.62% 桌面专项变化。
这不是新增硬件性能结论，也不增加圆角/文本的跨库覆盖范围。

这一边界比给出覆盖条件不一致的“总体快慢”数字更严格，也更适合定位下一项
Render Core 优化：先用内部 probe 判断算法变化，再用固定适配器或真实设备确认
端到端收益。
