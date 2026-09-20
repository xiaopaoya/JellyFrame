# 原生抗锯齿质量合同

> 最后更新：2026-09-19；适用版本：0.6.0-dev
> 状态：实验性质量/成本工具合同，不是 Runtime 发布门，也不是性能排名

## 目的和边界

逐像素等价对照回答“相同结果要花多久”；不同原生 AA 的质量/成本对照还必须回答
“画出的结果有什么差别”。本工具分别记录质量和已完成绘制成本，不改动
`rounded-card-rgb-v1` 的 exact coverage 门槛，不用 Core 输出作为其他库的理想答案。

范围固定为 172x320、整数坐标、统一圆角半径、不透明白色形状和黑底。白色 mask
消除低对比颜色的量化掩盖，8 位灰度解释为几何覆盖率，不做 sRGB 显示亮度评价。
当前不覆盖独立四角半径、描边、变换、阴影、透明目标、彩色混合或完整 UI。

## 独立参考

每个像素对应单位正方形 `[x,x+1] × [y,y+1]`，参考值是解析圆角形状在其中的连续面积。
固定 fixture 为 `rounded-uniform-v0`，包含卡片、矩形对照、小/最大半径、奇数尺寸、
左右/底部裁剪和 2x2 形状。几何、像素格式或 fixture 变化须升级合同。

参考算法使用整数坐标的四叉细分，深度 6。根据子方格到内缩矩形的距离上下界，
将完全在形状内部/外部的格子确定为满/空，只有穿越圆弧的格子继续细分。
最后不能确定的面积保留为区间，而不是取中心点冒充精确覆盖率。
单元测试用四分之一圆的 `pi/4` 和圆角矩形总面积公式交叉校验，并检查细分收敛、
对称和负坐标平移。这不是对 Core 4x4 点采样实现的复制。

## 实验门槛

策略标识为 `rounded-area-quarter-v0`，在运行 native adapter 前固定：

- 完全内部像素必须为 255，完全外部像素必须为 0；任何错误都失败。
- 仅在几何边缘像素计算 RMS，避免大片黑底或内部区域稀释边缘缺陷。
- 最大覆盖率误差不超过 `1/4`，边缘 RMS 不超过 `1/8`，单位为一个像素面积的比例。
- 误差下界超限则 `fail`，误差上界全部不超限才 `pass`；否则 `indeterminate`。
- 同时记录内部/外部错误数、边缘像素数、最大/RMS 误差区间和有符号总面积偏差。
- 一个 backend 必须全部 fixture 通过才得到整体 `pass`。不确定项不算通过。

这是一档可复核的实验质量预算，不是“人眼看不出差别”的结论。Core 与参考库一视同仁；
两者都可能失败。任何阈值变更必须另立版本，不得为了放行某次观测而修改本策略。
原有 quarter-origin 采样满足其历史 exact 合同，也仍可能偏离连续面积参考，二者不矛盾。

## 开发入口

```powershell
build\current-release\Release\jellyframe_cpu2d_compare.exe build\aa-quality 1 rounded-quality-masks
python tools\rounded_aa_quality.py --input build\aa-quality\coverage.json --output build\aa-quality\quality.json --markdown-output build\aa-quality\report.md
```

导出器生成 Core、GDI+ 原生 AA、GDI+ 二值负对照的覆盖率 PGM 和声明文件。
原生 AA 配置明确为 `SmoothingModeAntiAlias`、`PixelOffsetModeHalf`、
`SourceOver`、`CompositingQualityAssumeLinear`；二值对照采用 None/Half/SourceCopy。
结论仅适用于这一 adapter 配置，不代表整个 GDI+ 或所有 Windows 版本。
旧的 4 倍分辨率 exact 资格检查保持原配置，未被本合同替代。

分析器验证固定几何、全部用例、文件尺寸、灰度格式和路径边界；拒绝重复 backend、
缺失用例、目录外 mask 和覆盖输入文件。输出保留声明文件与每张 mask 的 SHA-256。
JSON 格式为 `jellyframe.rounded.quality.v0`，包含 `performanceMeasured: false` 和
`performanceComparable: false`。退出码 0 表示分析完成，不保证质量通过。
`benchmark_compare.py` 拒绝该格式，不能把质量结果混入现有速度排名。

## 完成绘制成本

```powershell
build\current-release\Release\jellyframe_cpu2d_compare.exe build\aa-cost 500 rounded-quality-cost
python tools\rounded_aa_quality.py --input build\aa-cost\coverage.json --cost-input build\aa-cost\cost.json --output build\aa-cost\joint.json --markdown-output build\aa-cost\report.md
```

计时合同 `rounded-native-completed-draw-v0`：

- 每 fixture/每 backend 预热 30 次，正式采集 1–10000 个独立 draw，默认 100；每样本只画一次。
- 复用 FrameBuffer、GDI+ Bitmap/Graphics 和白色 brush，创建与销毁这些宿主资源不计时。
- 每次将目标恢复为黑底并完成清屏同步，再开始计时，避免 AA 在上一帧边缘上反复累积。
- Core DisplayCommand 的构造、rasterize、销毁计时；GDI+ GraphicsPath 的构造、
  FillPath、销毁和同步 Flush 计时。不是预生成 mask blit，也不是仅测提交函数。
- 三个 backend 轮换起点并交替正反顺序，六轮覆盖全部排列；不固定让某个 backend 先执行。
- 覆盖率提取、颜色检查、PGM/JSON 写盘及 SHA-256 在计时外进行。输出来自最后一次计时 draw，
  不是重新绘制的另一张图；自动回归验证它与不计时的质量 fixture 一致。
- 原始单位为微秒，steady clock；报告按 nearest-rank 输出每个 case 的 p50/p95/min/max，
  同时保留原始数组，不换算 FPS，不混合用例或跨轮采样。输入数值须有限且在 0–1e12 微秒内。

导出器额外生成 `jellyframe.rounded.cost.v0`。分析器通过 backend/method、固定几何、
样本数量、计时合同和每张 PGM 的 SHA-256 关联质量与成本，拒绝错配或缺失的数据。
联合报告为 `jellyframe.rounded.quality-cost.v0`，`performanceMeasured: true`，但
`performanceComparable` 仍为 false；质量失败/不确定状态不会被计时数据覆盖。
所有成本标为 diagnostic-only，报告不生成速度比或赢家，现有等价性能比较器拒绝该格式。
即使个别用例通过实验质量门，也不能据此宣布两个 backend 输出等价。

Windows benchmark 使用系统 BCrypt 生成 mask SHA-256，需要 Windows 10+；这不是
Core、Runtime、设备固件或 SDK 的新依赖。性能证据必须用 Release，并避免同时运行构建；
Debug 仅作功能检查。该成本是本机 warm draw API 路径，不包括布局、脚本、present/DMA、
cold start 或设备帧耗时。没有新的硬件 A/B 要求。

## Core 内部成本探针

```powershell
build\current-release\Release\jellyframe_cpu2d_compare.exe build\rounded-probes 500 rounded-core-probes
```

生成 `probes.json` 和 `report.md`，合同为 `rounded-core-isolated-probes-v0`。沿用八项
固定 fixture、30 次预热、三路径六顺序轮换、每样本一次操作和 nearest-rank p50/p95。
原始微秒数组保留，不合并不同用例或重复轮次。三个路径分别为：

- `production-draw`：真实 Core 命令构造、rasterize 和销毁，与前述成本合同一致。
- `isolated-coverage`：对预先裁剪的四个角区调用实际 coverage helper，并写入预分配数组。
  计入遍历、计算和数组写入，不包括区域准备、像素混合或 framebuffer 写入。
- `cached-writes`：使用预先计算的 coverage，填充内部连续区间，角区满覆盖直接写入，
  部分覆盖混合。计入数组读取与像素写入，不包括 coverage 计算、命令或区域准备。

资源创建、分区、缓存分配和 reset 均在计时外；coverage probe 每次先将数组重置为 -1，
绘制路径每次先清黑。最后一次实际绘制与缓存回放均逐像素核对独立 quarter-grid oracle，
颜色三个通道及 alpha 都需一致；报告记录同一 PGM 序列化格式的 mask SHA-256。
自动回归另与原生 AA 质量导出的 Core mask 核对，并独立计算裁剪后分区计数。

`workCounts` 是**不计时的基准分区模型**：内部像素、角区像素、角区 0/255/部分覆盖
以及当前 helper 每角区像素 16 次采样的工作量。它不是运行时采集的硬件指令数。
分区只适用此固定统一圆角、不透明源、黑色不透明目标，不适用于所有圆角绘制。

两个隔离 probe **不是生产管线阶段计时**：准备工作、缓存访存、分支和编译内联与完整
绘制不同。不能相加、相减 p95 或给出“coverage 占帧时间百分比”；微小用例的 0 微秒
表示时钟分辨率下取整，不代表无成本。缓存回放是诊断实验，不是已实现的生产缓存方案。
JSON 为 `jellyframe.rounded.core-probes.v0`，标记 `productionPhaseTimings: false` 和
`performanceComparable: false`；现有跨库比较器拒绝该格式。

## 全满/全空采样早退候选

已将探针结果落实为保守 shortcut：对当前像素四个采样坐标的 dx/dy 区间分别求最大
距离和最小距离；只有能证明 16 个采样点全部在圆内或全部在圆外时，才分别返回 255
或 0。无法证明时完整保留原有 16 点循环。shortcut 使用与原循环相同的饱和乘法、
平方和与极值坐标处理，不移除溢出保护，也不改变 `sampled` 统计的含义。

候选验证包括八项 fixture 的生产绘制、预缓存回放、独立 oracle、最终 RGBA 和 mask
SHA-256；Corner shortcut 另有单元测试覆盖全满、全空和部分覆盖三类。三轮 Release
探针显示标准卡片的 production p50 从优化前约 151 us 降至 29.6--29.7 us，最大半径
用例从约 596.5 us 降至 97 us；这是本机隔离 draw 诊断，不是设备帧时间或生产阶段
占比。优化前后八项 mask hash 一致。

该改动不新增硬件测试要求，因为它未改变 Runtime ABI、采样网格、设备配置或输出契约。
后续应在 CI 完成后再决定是否对非均匀圆角、描边和 rounded clip 路径分别建立同等
证据；不得把 uniform opaque-fill 的收益外推到这些路径。

## 非均匀圆角填充

非均匀半径现在按行拆分：四个角候选区间继续调用原 coverage helper，区间之外的
连续不透明像素直接整段写入。候选区间按原 coverage 的角点分支顺序处理，因此半径
重叠时仍保留原有优先级；裁剪、负坐标和部分覆盖仍由原几何/coverage 逻辑处理。

新增全像素 reference 回归覆盖普通和裁剪/负坐标的非均匀半径，并比较 RGBA 全部像素。
桌面 Release microbench 的固定 workload 为 36 个 44x34 控件、半径 `{12,6,3,0}`，
三轮 baseline 为 368.245/385.400/367.175 us，候选为 175.635/174.935/179.575 us。
该 microbench 包含既有 target/rasterizer setup，数值只作同机前后信号，不是设备帧时间。
本轮没有改变采样网格、Runtime ABI 或设备配置，因此不新增硬件测试要求。

## 圆角描边边界

现有 `stroke_rounded_rect` 已在较早的 `13264bcd` 路径中完成包围盒拆分：内圈之外的
中间行只保留左右边带和实际圆角候选区，顶部/底部行才检查完整横向范围；coverage
helper 继续负责外圈减内圈的覆盖率差分。本轮补充了普通、裁剪、负坐标、非均匀半径、
奇数宽度的全像素 RGBA reference 回归，并在 Core microbench 增加 uniform/non-uniform
stroke 入口。

固定 36 个 44x34 控件的本机 Release 参考值约为 uniform stroke `693.695 us`、
non-uniform 1px stroke `350.305 us`。该数字仅用于后续对照；当前没有足够证据支持再
对描边做更激进的区间改写，因此本轮不修改描边生产算法，不新增硬件测试要求。
