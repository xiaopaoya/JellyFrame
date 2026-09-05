# 响应式布局契约

> 最后更新：2026-08-30；适用版本：0.6.0-dev
>
> 状态：Render Core 0.6.x 的实现基线。本文件描述 JellyFrame 行为契约，
> 不代表浏览器兼容性。

JellyFrame 的响应式布局是有边界的整数像素布局模型。App 必须针对目标
viewport 编写，并在每个声明支持的目标上检查。只有下表列出的属性、值形
式和所属布局模式，并且目标 feature profile 已启用时，CSS 声明才属于可用
契约。

## 坐标模型

- 布局原点是目标 viewport 左上角。
- 解析后的尺寸和坐标是非负整数 CSS 像素；文档化的脱离文档流偏移可以为负。
- viewport 宽高由布局和 `@media` 使用的明确目标输入决定，不取桌面窗口或开发者显示器尺寸。
- 当前公开 writing mode 只有 LTR。逻辑 `inline-*`、`block-*` 以及逻辑间距仅在该模式下映射到物理属性；垂直书写和双向布局延期。
- 圆屏目标会按目标形状裁剪绘制。圆屏不是第二个布局宽度，重要内容应放在矩形安全区域内。

## 尺寸

| 形式 | 状态 | 契约 |
| --- | --- | --- |
| 未声明宽高 | 支持 | 按所属布局模式使用 intrinsic 或 containing block 尺寸。 |
| 显式 `auto` 宽高 | 支持 | 等价于未声明，不产生样式 warning；它不是 `100%` 的别名。 |
| 非负 `px` | 支持 | 解析为整数像素尺寸。 |
| 百分比宽高 | 有限支持 | 相对 containing block 对应的可用尺寸解析。百分比高度需要确定的 containing height，否则使用 intrinsic 尺寸。 |
| `min-*` / `max-*` 的 `px` 或 `%` | 有限支持 | 在首选尺寸后、摆放前应用，并受目标整数布局范围限制。 |
| `em`、`rem`、`vw`、`vh`、有限 `calc()` | 有限支持 | 在 style resolution 阶段转为整数像素；`calc()` 不是通用表达式语言。 |
| 负数、非有限或不支持的长度 | 拒绝 | 忽略声明并报告属性级诊断，不得静默变成无关尺寸。 |
| min/max 尺寸使用 `auto` | 不属于作者契约 | 省略声明表示没有对应限制。 |

文档化的盒模型支持 `box-sizing: border-box`。未使用时，声明宽高是
content-box 尺寸。padding 和 border 必须计入目标预算，不要依赖浏览器
隐藏溢出来掩盖尺寸错误。

## Flex 与 Grid

Flex 是普通 App 响应式布局的首选原语：

- 支持 `row` 和 `column`。
- 直接子节点支持有限的 `flex-grow`、`flex-shrink`、`flex-basis`、`order` 和 `align-self`。
- `align-items` 和 `justify-content` 支持 `start`、`center`、`end` 及文档化的分布值；交叉轴的 `stretch` 有效。值会按当前轴解析，不是绝对定位的互换别名。
- `stretch` 只改变交叉轴为 `auto` 的尺寸；显式交叉轴宽高保持优先，margin、padding 和 border 都计入可用交叉轴范围。
- `gap`、`row-gap`、`column-gap` 是有界像素/整数值。
- `flex-wrap: wrap` 是有限的 row 换行模式；column flex 保持不换行，`wrap-reverse` 会被拒绝而不是近似处理。它不替代滚动，也不提供浏览器的行平衡算法；每个换行后的 line 都有独立的交叉轴对齐上下文。
- Flex 尺寸以整数并受边界限制，余数按源码顺序确定性分配。

Grid 保持较窄的范围：使用 2 到 4 个固定或 `1fr` 轨道、有限的
`minmax()`，以及正数的 placement/span。复杂 auto-placement、命名区域、
subgrid 和 content-driven 轨道算法不属于承诺范围。

## 响应式规则

公开契约只包含使用有限像素值的 `@media` 条件：`min-width`、`max-width`、
`min-height`、`max-height`。宽高同时影响布局时应分别编写规则。媒体规则
在 style resolution 前按实际目标 viewport 求值。不支持的媒体特性会被报告，
不会建立隐藏的 fallback 模式。

建议模式：

```css
.screen { display: flex; flex-direction: row; gap: 8px; }
.card { flex: 1 1 0; min-width: 0; max-width: 100%; }
@media (max-width: 200px) {
  .screen { flex-direction: column; gap: 4px; }
  .card { width: 100%; }
}
```

可能超过小屏幕的内容应放入明确固定高度的 `overflow-y: auto` 区域。页面
变高不会自动获得滚动能力；需要固定在底部的导航应放在滚动区域之外。

## 文本与控件

文本测量属于布局而非仅绘制。稳定控件应显式指定 `font-size` 和
`line-height`。支持的换行范围是 UTF-8 scalar 边界、正常断点，或
`white-space: nowrap` 加 UTF-8 安全省略号。连字、平衡换行、复杂文字 shaping
和自动浏览器字体替换不在保证范围内。

每个有文字的目标都需要字体报告。缺少请求的字体 face 或 weight 时，应打包
匹配的 `.jffont` 或调整设计，不能把桌面和设备布局相同当作已被证明。

## 验收矩阵

作者可用的布局改动只有在满足以下条件后才算完成：

1. 每种属性和值形式都有正向和负向 parser/style 测试。
2. 在 `300x300`、`320x240`、`172x320` 上有布局测试，包含窄屏 media 分支；形状影响绘制时还要包含圆屏目标。
3. 有确定性的桌面截图，且没有意外布局溢出或不支持样式诊断。
4. 更新 capability table，并为拒绝的形式提供可执行诊断文字。
5. 新增布局 pass、缓存或逐节点分配时，记录 benchmark 或有界内存测量。
6. 在称为 port-supported 前取得实机证据。

后续 Core 候选应由可复现的作者需求选择，而不是由 CSSWG 中标记为
`partial` 的属性数量决定。
