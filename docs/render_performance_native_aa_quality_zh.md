# 原生抗锯齿质量合同

> 最后更新：2026-09-19；适用版本：0.6.0-dev
> 状态：实验性工具合同，不是 Runtime 发布门，也不是性能排名

## 目的和边界

逐像素等价对照回答“相同结果要花多久”；不同原生 AA 的质量/成本对照还必须回答
“画出的结果有什么差别”。本工具只完成第二类对照的质量部分，不改动
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

## 后续接入

本轮没有计时，也不要求硬件 A/B。下一步是独立的质量/成本联合报告：保留每个 adapter
的质量状态和实际执行路径，冻结分配、mask 生成、采样归一化及完成同步的计时边界。
未通过质量门的结果只能作为诊断，不能与 exact 对照混排或声称同质量下更快。
不能为完成基准而默认更改 Core 的采样网格、像素基线或设备发布配置。
