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

下一步沿 Core 自身固定输出做圆角成本归因，区分 coverage 与内部填充工作；此报告不授权
默认修改采样网格、像素基线或设备发布配置，也不替代真实 workload 的优化验收。
