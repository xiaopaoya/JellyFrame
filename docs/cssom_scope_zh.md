# CSSOM 与 Cascade 裁剪范围

最后对照 CSSOM 和 CSS Cascade 相关资料的时间：2026-06-13。

- CSSOM: https://www.w3.org/TR/cssom-1/
- CSS Cascading and Inheritance: https://www.w3.org/TR/css-cascade-5/
- CSS Syntax: https://www.w3.org/TR/css-syntax-3/
- Blink CSS sources:
  https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/css/
- WebKit CSS sources:
  https://github.com/WebKit/WebKit/tree/main/Source/WebCore/css

WearWeb 需要一个小型 CSSOM，用来保留作者意图，并给后续 runtime 层提供稳定
对象模型。第一版 CSSOM 不是完整 JavaScript 暴露 API，而是 parsing、cascade、
style resolution、诊断工具和后续 JerryScript 绑定共用的内部表示。

## 已实现模型

```text
CssStyleSheet
  -> CssRule list
      -> CssRuleType::Style
      -> selector text
      -> ordered CssDeclaration list
      -> CssSpecificity
      -> source order
```

公开命名刻意贴近 CSSOM 概念：

- `CssStyleSheet`
- `CssRule`
- `CssRuleType`
- `CssDeclaration`
- `CssSpecificity`

## Cascade 策略

当前 cascade 只覆盖 author style：

- 匹配已支持 selector。
- 已支持 selector 包括简单 compound selectors、descendant combinators、child
  combinators、简单 attribute selectors 和 `:root`。
- 比较 `!important`。
- 比较 selector specificity。
- 比较 source order。
- inline style 作为高 specificity 的 author declaration 应用。
- 不支持的 value 不会清除之前已经支持的 fallback value。

这足以保持功能性 UI 结构稳定。例如现代搜索页可能失去 blur、shadow、圆角或
高级颜色空间，但只要存在 fallback，基础框、颜色、尺寸和间距声明应当保留。

## 懒处理

- 尚未建模 user-agent origin、user origin 和 animation origin。
- Cascade layers 在 parse 阶段展开；尚未建模 layer ordering。
- Custom properties 会作为 declaration 保留，但暂不解析依赖关系。
- sibling combinators 和动态 pseudo-classes 等不支持 selector 会在插入 CSSOM 前
  整体跳过，或在 matching 阶段忽略。
- 不支持 value 会留在 CSSOM 中用于诊断，但不会覆盖已支持 computed value。
- 在完整 UA stylesheet 出现前，内置小型 default style layer，让 form controls、
  dialog 和 media elements 至少有可用框体。

## 低性能设备规则

- CSSOM construction 必须受 parser limits 约束。
- 当前 style resolution 保持对 parsed style rules 的线性扫描。
- 不支持现代特性应当干净跳过，不能触发昂贵恢复循环。
- 诊断 dump 工具限制输入大小和输出长度。

## 当前面向 UI 的 computed fields

- `display`：block、inline、inline-block、flex、grid、none。
- `margin`、`padding`、`border-width`、`border-color`、`border-radius`。
- `width`、`height`、`min-width`、`min-height`、`max-width`。
- `color`、`background-color`。
- `font-size`、`line-height`、`text-align`、`text-indent`。
- `box-sizing`、`overflow`、`opacity`、`position`、`z-index`、`transform`。
- 面向嵌入式应用的 layout 字段：`aspect-ratio`、`gap`、`column-gap`、
  `row-gap`、简化 grid 最小轨道尺寸、`grid-auto-rows` 最小尺寸，以及
  `grid-column`/`grid-row` span。
- `box-shadow` 已存储，并绘制为便宜的圆角半透明矩形；blur 只做近似，不做真实栅格模糊。
