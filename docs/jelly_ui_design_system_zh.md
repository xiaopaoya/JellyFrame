# JellyFrame 胶体界面系统

本文定义 JellyFrame 的第一套原生视觉与动效体系。它把项目名里的
`Jelly` 解释为水母和果冻共享的胶体特性：柔软膜面、半透明厚度、内部微光、
轻微浮力和受压回弹。目标不是复刻 Material、Cupertino、Fluent 或国内大厂
组件库，而是在 JellyFrame 当前可用能力内形成可落地、可测试、可降级的
嵌入式 UI 语言。

## 设计目标

- 让 JellyFrame app 一眼能被识别为胶体、水母、果冻气质，而不是普通卡片 UI。
- 只依赖当前运行时能稳定兑现的 HTML/CSS/render-core 能力。
- 所有视觉效果都有低成本降级路径，不能依赖浏览器专有的 blur、blend、SVG 或
  Canvas。
- 动效以小范围 paint/compositor 属性为主，避免每帧 layout。
- 在低功耗设备上可预算、可关闭、可限帧。

## 当前实现基线

设计系统必须以当前项目能力为准：

- 可依赖：`rgba()`、hex、基础命名色、`background-color`，以及简单
  `background: linear-gradient(<color>, <color>)` 垂直胶体表面、单值
  `border-radius`、border、padding、margin、`box-shadow`
  近似、`opacity`、`transform: translate()/scale()`、`transition` 子集、
  `:hover`、`:active`、`:focus`、`:focus-within`、`:checked` 和 `:disabled`。
- 可依赖：CSS custom properties 的直接 `var(--token)` 和 fallback。
- 可依赖：CSS `transition` 和 `transition-*` 的有界列表，单个 style 最多保留四条
  transition entry。当前可动画属性是
  `opacity`、`transform`、`background-color` 和 `color`。
- 克制使用：有界 `@keyframes` / `animation-*` from/to 动画，属性限于
  `opacity`、`transform: translate()/scale()`、`background-color` 和 `color`。
  它适合少量常驻 loading/pulse 状态，不适合 layout motion。
- 可依赖：软件合成器对 composited layer 执行 `translate()/scale()`，平移会取整，
  缩放以 layer bounds 中心为原点并用最近邻采样。
- 可依赖：JerryScript 构建中的 `requestAnimationFrame()` 和
  `cancelAnimationFrame()`，由宿主以每帧预算泵动。
- 不依赖：`backdrop-filter`、CSS `filter`、`mix-blend-mode`、SVG 纹理、
  Canvas 绘制、layout 属性动画、rotate、skew、matrix、perspective、完整
  `transform-origin`。

## 可行性分析

这条方向在当前引擎上是可行的，因为它把 JellyFrame 当作小型 HTML/CSS app runtime，
而不是试图复刻桌面浏览器的复杂视觉效果。

- 视觉识别主要来自便宜原语：颜色、opacity、border、单值圆角、近似 shadow 和少量
  子元素高光。
- 动效只落在 paint/compositor 属性上，静态 app 不需要为 layout animation 付费。
- 控件仍是普通 HTML 控件或 button-like 元素，因此事件、focus、disabled 状态和脚本绑定可以继续工作。
- 低功耗产品可以降低 `animation_frame_rate`，甚至把 animation budget 设为 0；每个动效状态都有静态等价形态，
  因此 UI 仍保持可读。

需要警惕的部分被刻意排除在标准之外：blur、blend mode、canvas 控件、任意 SVG 结构、matrix transform 和
layout motion。它们可以出现在设计稿中，但实现必须提供 JellyFrame 安全降级。

## 已落地示例

- `samples/apps/packages/jelly_controls`：完整可安装 source package，展示胶体按钮、输入框、
  switch、进度条和小型 keyframe pulse。
- `samples/apps/loose/jelly_motion.html`：聚焦动效 fixture，只使用 transition 和 from/to keyframes。
- `samples/apps/loose/jelly_launcher_mock.html`：小型启动器风格 app grid。
- `samples/apps/system/sample_launcher`：真实 sample launcher 已按同一胶体 panel/button 方向调整。

## 设计内核

JellyFrame 的胶体语言由四个概念组成：

| 概念 | 视觉含义 | 工程表达 |
| --- | --- | --- |
| Membrane | 半透明柔软外膜 | `rgba` 背景、细边框、圆角 |
| Core Glow | 内部微光和厚度 | 内层元素、高光条、浅色边缘 |
| Buoyancy | 轻微浮力 | `opacity + translateY` 过渡，少量场景使用 rAF |
| Soft Press | 受压回弹 | `:active` 上的 `scale()` 和颜色变化 |

一句话原则：**不是玻璃拟态，不是磨砂，是含水凝胶。**

## 色彩 Tokens

默认主题是 `native-jelly`。它使用浅海蓝作为胶体主色，并用珊瑚、青柠作为
状态点，避免整套界面只剩单一蓝色。

```json
{
  "theme": "native-jelly",
  "surface.light": "#F6FBFF",
  "surface.dark": "#0F1A29",
  "ink": "#101820",
  "mutedInk": "rgba(16, 24, 32, 0.68)",
  "gel": "rgba(156, 224, 247, 0.45)",
  "gel.thick": "rgba(82, 170, 204, 0.22)",
  "gel.highlight": "rgba(255, 255, 255, 0.28)",
  "gel.edge": "rgba(100, 200, 235, 0.60)",
  "glow.coral": "rgba(255, 126, 103, 0.55)",
  "glow.lime": "rgba(183, 243, 107, 0.55)",
  "state.success": "rgba(115, 222, 184, 0.50)",
  "state.warning": "rgba(255, 204, 136, 0.50)",
  "state.danger": "rgba(236, 134, 164, 0.50)"
}
```

可选主题：

- `peach-gel`：蜜桃果冻。适合健康、休息、生活方式 app。
- `lime-gel`：青柠凝胶。适合运动、计时、状态监控 app。
- `deep-jelly`：深海夜光。适合暗色设备、表盘、夜间模式。

主题实现建议使用 CSS custom properties：

```css
:root {
  --jf-surface: #f6fbff;
  --jf-ink: #101820;
  --jf-muted-ink: rgba(16, 24, 32, 0.68);
  --jf-gel: rgba(156, 224, 247, 0.45);
  --jf-gel-thick: rgba(82, 170, 204, 0.22);
  --jf-gel-highlight: rgba(255, 255, 255, 0.28);
  --jf-gel-edge: rgba(100, 200, 235, 0.60);
  --jf-coral: rgba(255, 126, 103, 0.55);
  --jf-lime: rgba(183, 243, 107, 0.55);
}
```

## 材质模型

运行时 v1 使用三层结构，避免依赖真实 blur 和 blend mode。

### 1. Gel Base

基础胶体面。用于按钮、面板、输入框、进度条轨道。

```css
.jf-gel {
  background-color: var(--jf-gel);
  color: var(--jf-ink);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 14px;
  box-shadow: 0 0 12px rgba(100, 200, 235, 0.20);
}
```

### 2. Core Highlight

使用真实子元素表达内部高光，而不是 `backdrop-filter`。

```html
<button class="jf-button">
  <span class="jf-core"></span>
  <span class="jf-label">Start</span>
</button>
```

```css
.jf-button {
  position: relative;
  overflow: hidden;
  background-color: var(--jf-gel);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 999px;
}

.jf-core {
  position: absolute;
  left: 10px;
  right: 10px;
  top: 3px;
  height: 8px;
  background-color: var(--jf-gel-highlight);
  border-radius: 999px;
}

.jf-label {
  position: relative;
}
```

### 3. Edge Thickness

使用边框和底部加深色表达厚度。当前 `border-radius` 只应使用单值，不把四角不规则轮廓作为必需能力。

```css
.jf-panel {
  background-color: var(--jf-gel);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 18px;
  box-shadow: 0 0 16px rgba(82, 170, 204, 0.18);
}
```

## 组件规范

### Jelly Button

用途：主操作、次操作、表盘快捷键、计算器按键。

行为：

- 默认状态是半透明胶囊。
- `:hover` 或 focus 时提高边缘亮度。
- `:active` 时轻微下压和缩放。
- disabled 时降低透明度，停止高光。

```css
.jf-button {
  min-height: 36px;
  padding: 8px 14px;
  color: var(--jf-ink);
  background-color: var(--jf-gel);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 999px;
  transition: transform 160ms ease-out, background-color 160ms ease-out,
              opacity 160ms ease-out, color 160ms ease-out;
}

.jf-button:focus,
.jf-button:hover {
  background-color: rgba(156, 224, 247, 0.58);
}

.jf-button:active {
  transform: translate(0px, 1px) scale(0.96);
  background-color: var(--jf-gel-thick);
}

.jf-button[disabled],
.jf-button:disabled {
  opacity: 0.45;
}
```

### Jelly Panel

用途：信息组、设置组、弹出面板、toast。不要把所有页面区域都做成卡片，JellyFrame
应用仍应优先清晰和省电。

```css
.jf-panel {
  padding: 12px;
  background-color: var(--jf-gel);
  color: var(--jf-ink);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 18px;
  transition: opacity 220ms ease-out, transform 220ms ease-out,
              background-color 220ms ease-out;
}

.jf-panel.is-entering {
  opacity: 0;
  transform: translate(0px, 8px) scale(0.98);
}

.jf-panel.is-visible {
  opacity: 1;
  transform: translate(0px, 0px) scale(1);
}
```

### Jelly Input

用途：短文本、搜索、配置项。输入框要像柔软膜面包住内容，而不是硬边矩形。

```css
.jf-input {
  min-height: 34px;
  padding: 7px 10px;
  color: var(--jf-ink);
  background-color: rgba(255, 255, 255, 0.54);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 999px;
  transition: background-color 160ms ease-out, color 160ms ease-out;
}

.jf-input:focus {
  background-color: rgba(255, 255, 255, 0.78);
}

.jf-input.is-danger {
  border-color: var(--jf-coral);
  background-color: rgba(236, 134, 164, 0.18);
}
```

### Jelly Switch

用途：二值设置。Switch 是最适合胶体语言的控件之一：轨道是膜，thumb 是凝胶核。

```html
<button class="jf-switch is-on" role="switch" aria-checked="true">
  <span class="jf-switch-thumb"></span>
</button>
```

```css
.jf-switch {
  width: 48px;
  height: 28px;
  padding: 3px;
  background-color: rgba(82, 170, 204, 0.22);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 999px;
  transition: background-color 180ms ease-out;
}

.jf-switch-thumb {
  display: block;
  width: 20px;
  height: 20px;
  background-color: rgba(255, 255, 255, 0.82);
  border-radius: 999px;
  transition: transform 180ms ease-out, background-color 180ms ease-out;
}

.jf-switch.is-on {
  background-color: var(--jf-lime);
}

.jf-switch.is-on .jf-switch-thumb {
  transform: translate(20px, 0px) scale(1.03);
}
```

### Jelly Slider

用途：音量、亮度、阈值。轨道像触手胶体，thumb 像水滴球。

```css
.jf-slider {
  height: 28px;
  padding: 10px 0;
}

.jf-slider-track {
  height: 8px;
  background-color: rgba(82, 170, 204, 0.22);
  border-radius: 999px;
}

.jf-slider-fill {
  height: 8px;
  background-color: var(--jf-gel-edge);
  border-radius: 999px;
  transition: background-color 160ms ease-out;
}

.jf-slider-thumb {
  width: 20px;
  height: 20px;
  background-color: rgba(255, 255, 255, 0.84);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 999px;
  transition: transform 140ms ease-out;
}

.jf-slider-thumb:active {
  transform: scale(1.08);
}
```

### Tide Progress

用途：加载、下载、同步、计时进度。优先使用圆角条和移动光带，不使用 Canvas。

```css
.jf-progress {
  height: 10px;
  background-color: rgba(82, 170, 204, 0.20);
  border-radius: 999px;
  overflow: hidden;
}

.jf-progress-value {
  height: 10px;
  background-color: var(--jf-gel-edge);
  border-radius: 999px;
  transition: transform 180ms linear, background-color 180ms ease-out;
}
```

确定进度建议由宿主或脚本设置 width/transform。若要平滑，优先改变 `transform`
或颜色，不要每帧改 layout 属性。

### Gel Dialog

用途：确认、短设置、错误恢复。v1 使用浮起，不承诺触发点径向展开。

```css
.jf-dialog {
  padding: 14px;
  background-color: var(--jf-gel);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 20px;
  opacity: 0;
  transform: translate(0px, 10px) scale(0.96);
  transition: opacity 240ms ease-out, transform 240ms ease-out;
}

.jf-dialog.is-open {
  opacity: 1;
  transform: translate(0px, 0px) scale(1);
}
```

## 动效规范

Jelly UI 动效必须先走当前已支持的 CSS transition 子集。只有需要精确逐帧控制时才使用
`requestAnimationFrame`。

### Motion Tokens

| Token | 时长 | 属性 | 用途 |
| --- | --- | --- | --- |
| `jelly.press` | 140-180ms | `transform`, `background-color`, `opacity` | 按钮、按键、switch thumb |
| `jelly.float-in` | 200-260ms | `opacity`, `transform` | dialog、toast、popover |
| `jelly.settle` | 180-240ms | `transform` | 开关、滑块、卡片轻微复位 |
| `jelly.pulse-core` | 900-1400ms | `opacity`, `background-color` | 少量 active/loading 状态 |

### 推荐曲线

当前 parser 支持 `linear`、`ease`、`ease-in`、`ease-out`、`ease-in-out`。

- 按压反馈使用 `ease-out`。
- 进入/弹出使用 `ease-out`。
- 退出使用 `ease-in`。
- 进度条稳定移动使用 `linear`。
- 不要在文档中要求 `cubic-bezier()`，当前不是稳定能力。

### CSS Transition 示例

```css
.jf-toast {
  opacity: 0;
  transform: translate(0px, 8px) scale(0.98);
  transition: opacity 220ms ease-out, transform 220ms ease-out;
}

.jf-toast.is-visible {
  opacity: 1;
  transform: translate(0px, 0px) scale(1);
}
```

### rAF 使用边界

`requestAnimationFrame` 只用于以下场景：

- loading 光带或 active sensor 呼吸，且同时活跃数量很少。
- 需要读取宿主时间戳的计时类 UI。
- JS 驱动 class/attribute 切换后仍无法表达的特殊交互。

必须遵守：

- rAF callback 是 one-shot，下一帧需要重新注册。
- 宿主可以把 animation callback/FPS 预算设为 0。
- callback 只做小量 DOM/style 更新，不做大量节点创建。
- 息屏、后台、低功耗模式必须停掉非必要动效。

## 禁用和降级规则

禁止把这些能力写进 Jelly UI v1 必需样式：

- `backdrop-filter`
- `filter`
- `mix-blend-mode`
- SVG 纹理或 SVG 图标作为控件必需结构
- Canvas 绘制常规控件
- layout 属性动画和完整 CSS animation 语义
- rotate、skew、matrix、perspective
- 四角不一致的 `border-radius` 作为必需效果

允许在桌面设计稿中出现，但实现必须提供 JellyFrame v1 降级：

- 不规则水母伞体轮廓降级为单值圆角面板。
- 真实模糊降级为半透明色和近似 shadow。
- 噪点/气泡纹理降级为少量静态高光子元素。
- 径向展开降级为 `opacity + translate + scale`。

## 版式和密度

- 可穿戴目标优先 300x300、320x240、390x640 等紧凑 viewport。
- 不采用严格 8px 栅格，但尺寸仍应稳定。推荐间距：4、6、8、12、16。
- 控件最小命中高度建议 32px，表盘和触控设备建议 36px 以上。
- 文本颜色避免纯黑大面积使用，默认使用 `--jf-ink` 和 `--jf-muted-ink`。
- 页面不应全是漂浮卡片。背景、分组、主操作要有清楚层级。

## 主题文件建议

建议新增主题资源目录：

```text
tools/templates/themes/jelly/
  jelly.tokens.json
  jelly.css
  components/
    button.html
    input.html
    switch.html
    slider.html
    progress.html
    dialog.html
  diagnostics/
    jelly_theme_rules.json
```

`jelly_theme_rules.json` 应被未来能力校验器读取，用于报告：

- 使用了 Jelly UI 禁用属性。
- 动画属性超出 `opacity`、`transform`、`background-color`、`color`。
- 使用了完整 CSS animation 语义或目标 profile 禁用了 animation FPS。
- 持续动效数量超过目标预算。
- 控件未提供 disabled/focus/active 状态。

## 开发落地顺序

1. 建立 `jelly.tokens.json` 和 `jelly.css`，只包含安全 v1 能力。
2. 先实现 Button、Panel、Input、Switch、Progress。
3. 为每个组件提供 300x300 round target 的 sample page。
4. 通过 `jellyframe_pseudo_browser` 生成 BMP/PPM，确认无空白、无重叠、无文字溢出。
5. 通过 `jellyframe_win32_browser` 验证 hover、active、focus 和 transition。
6. 更新能力校验器，让它识别 Jelly UI 禁用属性和动画预算。
7. 再考虑 rAF loading/pulse 模板。

## 验收清单

- 默认、hover、active、focus、disabled 状态都存在。
- 不使用 forbidden 列表中的能力作为必需视觉。
- 主要动效只改变 `opacity`、`transform`、`background-color` 或 `color`。
- 没有依赖 Canvas 的常规控件。
- 在 300x300 viewport 内文本不溢出、不互相遮挡。
- 动画结束后最终样式与静态状态一致。
- 低功耗 profile 下关闭持续动效后 UI 仍完整可用。
- package report 无字体缺字、资源缺失和预算超限。

## 与能力矩阵的关系

能力矩阵定义 JellyFrame 能做什么；本文定义 JellyFrame 应该如何看起来像自己。
当二者冲突时，以能力矩阵和源码为准。设计系统只能使用当前能力的稳定子集，
不把路线图能力伪装成已交付能力。
