# JellyFrame 可穿戴优先项目色卡

> 最后更新：2026-09-08；适用版本：0.6.0-dev；色卡版本：2

![JellyFrame 项目色卡](palette.png)

首要服务对象是小屏可穿戴设备，尤其是手表与手环；其他嵌入式屏幕为次要场景。保留已确定的 Logo 原色和色阶，以深底亮字、大数字、少量数据色和短时扫读为主要应用方式。色卡示例为设计参考，未自动修改现有 app 模板。

## Logo 的实际用色

| 角色 | HEX |
| --- | --- |
| highlight | `#67DDED` |
| primary | `#26B9DE` |
| ocean | `#1680C7` |
| depth | `#176DB1` |
| tentacleLeft | `#40C7DE` |
| tentacleCenter | `#239BCF` |
| tentacleRight | `#188BC8` |

伞盖渐变：`#67DDED`（0%）→ `#26B9DE`（48%）→ `#1680C7`（100%）；中间触手渐变：`#239BCF` → `#176DB1`。方向以 Logo SVG 的渐变坐标为准。

## 基础色板

| 类别 | 色号 / 色阶 |
| --- | --- |
| brand | 50 `#F0FCFE` · 100 `#DDF7FB` · 200 `#B6EDF5` · 300 `#8AE5F0` · 400 `#67DDED` · 500 `#26B9DE` · 600 `#1680C7` · 700 `#176DB1` · 800 `#15558A` · 900 `#153D61` · 950 `#10283F` |
| neutral | 0 `#FFFFFF` · 50 `#F6FBFF` · 100 `#E8F3FA` · 200 `#D5E4ED` · 300 `#ADC4D2` · 400 `#829DAC` · 500 `#607C8C` · 600 `#496373` · 700 `#344D5D` · 800 `#243747` · 900 `#0F1A29` · 950 `#101820` |
| accent | coral `#FF7E67` · lime `#B7F36B` · mint `#73DEB8` · amber `#FFCC88` · rose `#EC86A4` |

品牌 500 是视觉主色；品牌 700 是浅色界面的主要操作色。中性色 900 和 950 分别保留原主题深底与原字标墨色。辅助色中的 mint、amber、rose 是原有半透明状态色的 RGB 基色。

## 深浅主题语义

| Token | 浅色 | 深色 |
| --- | --- | --- |
| `background` | `#F6FBFF` | `#0F1A29` |
| `surface` | `#FFFFFF` | `#243747` |
| `surfaceRaised` | `#E8F3FA` | `#344D5D` |
| `text` | `#101820` | `#E8F3FA` |
| `textMuted` | `#496373` | `#ADC4D2` |
| `border` | `#D5E4ED` | `#344D5D` |
| `controlBorder` | `#607C8C` | `#829DAC` |
| `primary` | `#176DB1` | `#67DDED` |
| `primaryHover` | `#15558A` | `#8AE5F0` |
| `onPrimary` | `#FFFFFF` | `#101820` |
| `link` | `#176DB1` | `#67DDED` |
| `focus` | `#176DB1` | `#67DDED` |
| `selection` | `#DDF7FB` | `#15558A` |
| `onSelection` | `#153D61` | `#E8F3FA` |
| `success` | `#146B50` | `#73DEB8` |
| `successSurface` | `#E5F8F0` | `#173D35` |
| `warning` | `#85500A` | `#FFCC88` |
| `warningSurface` | `#FFF3DF` | `#42331F` |
| `danger` | `#A52D54` | `#EC86A4` |
| `dangerSurface` | `#FDECF1` | `#452536` |
| `info` | `#176DB1` | `#67DDED` |
| `infoSurface` | `#E8F3FA` | `#153D61` |

## 可穿戴主题（首选）

`.jf-theme-wearable` 继承深色语义，使用以下覆盖和数据色。适用于 OLED 手表、手环的活跃界面；LCD 或反射式屏幕按面板特性和户外实测选择深浅主题，纯黑底的功耗收益不适用于所有显示技术。

| Token | 值 |
| --- | --- |
| `background` | `#000000` |
| `surface` | `#121C28` |
| `surfaceRaised` | `#1C2C3C` |
| `primaryPressed` | `#26B9DE` |
| `heartRate` | `#FF7E67` |
| `activity` | `#B7F36B` |
| `progressTrack` | `#243747` |

`primaryPressed` 用于触控按下反馈；腕上交互不依赖 hover。珊瑚心率色和青柠运动色表示数据类别，不能直接表示健康异常或告警级别。提醒继续使用 warning，错误使用 danger，并配合文字或图标。

### 息屏表盘（AOD）

| Token | 值 |
| --- | --- |
| `background` | `#000000` |
| `time` | `#ADC4D2` |
| `text` | `#829DAC` |

AOD 是独立的显示建议：只保留时间和必要日期，去除进度环、渐变、大面积填充和按钮。色值本身不会启用息屏模式或保证功耗；低亮度、刷新间隔、像素位移与防烧屏策略由设备端实现并验证。

### 小屏布局与配色示例

- 300×300 圆表：时间为第一层，步数为第二层，进度环只作为辅助。重要信息居中并避开圆屏裁切区；示意边缘环允许使用边缘区域。
- 172×320 手环：单列显示运动时长、距离和心率，底部仅一个“暂停”操作。不要把桌面多列卡片等比缩小。示例按钮为 124×48 像素，实际触控目标应根据面板尺寸、像素密度、交互方式和触控误差验证。
- 色卡中的设备画面采用对应的逻辑像素尺寸；图片在文档中缩放后不代表真实物理尺寸。它们是设计示意，不是 JellyFrame 引擎截图。
- 活跃界面使用大面积暗底与留白，单屏通常一个品牌焦点，按需增加 1–2 种数据色。不要机械执行桌面页面的 70/20/10 色块比例。
- 主数字建议从 32–64 逻辑像素探索，关键标签从 16–20 像素探索；最终以字体包、面板像素密度、腕距和户外可读性校准。
- 优先使用实色文字和实色进度，凝胶高光限于小范围主动交互，常驻界面避免持续装饰动画；AOD 不使用凝胶叠层。
- RGB565、灰度和单色目标需要实机复核色差、条带、文字与进度辨识度。即使没有色彩，数值、标签和进度形状也应传达状态。

## 凝胶材质

以下数值保留现有设计系统定义。RGBA 是叠加层参数，最终观感取决于底色，不能当成固定 HEX 或直接作为文字对比度保证。

| Token | 值 |
| --- | --- |
| `gel` | `rgba(156, 224, 247, 0.45)` |
| `gelThick` | `rgba(82, 170, 204, 0.22)` |
| `gelHighlight` | `rgba(255, 255, 255, 0.28)` |
| `gelEdge` | `rgba(100, 200, 235, 0.60)` |
| `glowCoral` | `rgba(255, 126, 103, 0.55)` |
| `glowLime` | `rgba(183, 243, 107, 0.55)` |
| `stateSuccess` | `rgba(115, 222, 184, 0.50)` |
| `stateWarning` | `rgba(255, 204, 136, 0.50)` |
| `stateDanger` | `rgba(236, 134, 164, 0.50)` |

## 使用规则与接入

- 手表、手环优先使用可穿戴主题；其他嵌入式界面复用深浅主题与相同状态语义。
- 亮青优先用于标识、图形和高光。浅色主题的小字链接、白字按钮使用 `#176DB1`；不要直接在明亮的品牌 500 上叠加白色小字。
- `border` 用于装饰分隔；需要识别控件边界时使用 `controlBorder`。焦点环与控件之间留出背景色间隔。
- 状态同时配合文案或图标；珊瑚与青柠是点缀色，不默认绑定错误或成功。
- VS Code 内部控件继续使用 `--vscode-*` 主题变量，保持用户主题与高对比主题适配；本色卡适合项目自有界面和品牌素材。
- `palette.json` 是色卡数据源；运行 `python docs/assets/brand/generate_palette.py` 更新 CSS、SVG 和本文，并验证不透明颜色配对的对比度。PNG 是 SVG 的栅格预览，需在修改后重新导出。
- `palette.css` 使用 `--jf-brand-*`、`--jf-neutral-*`、`--jf-accent-*`、`--jf-material-*` 和 `--jf-color-*`，避开现有 `--jf-surface` 等模板变量。导入后将语义变量映射到需要的控件样式。

```css
/* 引入 palette.css，并为设备 app 的根容器添加 jf-theme-wearable。 */
.app { background-color: var(--jf-color-background); color: var(--jf-color-text); }
.primary-button { background-color: var(--jf-color-primary); color: var(--jf-color-on-primary); }
.jf-theme-wearable .primary-button:active { background-color: var(--jf-color-primary-pressed); }
.aod { background-color: var(--jf-aod-background); color: var(--jf-aod-time); }
```

CSS 保留默认浅色以兼容已有引用；腕上 app 显式使用 `jf-theme-wearable`。`jf-theme-light` / `jf-theme-dark` 可供其他设备选择。AOD 变量仅提供显示色值，不触发设备生命周期或电源管理。

Logo 的三色渐变用于 SVG 品牌素材。嵌入式 UI 按现有渲染能力使用两色渐变 `linear-gradient(#67DDED, #1680C7)`，不将 SVG 或三色渐变支持视为运行时前提。

## 对比度记录

按 WCAG sRGB 相对亮度计算，以下不透明文字配对通过 AA 普通文本 4.5:1；标注“非文本”的焦点与控件边界配对通过 3:1。该记录只覆盖列出的组合，不代表任意叠层、渐变或整套应用均通过检查。

| 主题 | 前景 / 背景 | 对比度 |
| --- | --- | --- |
| light | `text` / `background` | 17.18:1 |
| light | `text` / `surface` | 17.89:1 |
| light | `text` / `surfaceRaised` | 15.88:1 |
| light | `textMuted` / `background` | 6.08:1 |
| light | `textMuted` / `surface` | 6.33:1 |
| light | `link` / `background` | 5.22:1 |
| light | `link` / `surface` | 5.44:1 |
| light | `onPrimary` / `primary` | 5.44:1 |
| light | `onPrimary` / `primaryHover` | 7.78:1 |
| light | `onSelection` / `selection` | 10.03:1 |
| light | `success` / `successSurface` | 5.85:1 |
| light | `warning` / `warningSurface` | 6.08:1 |
| light | `danger` / `dangerSurface` | 5.94:1 |
| light | `info` / `infoSurface` | 4.83:1 |
| light | `focus` / `background`（非文本） | 5.22:1 |
| light | `focus` / `surface`（非文本） | 5.44:1 |
| light | `controlBorder` / `background`（非文本） | 4.24:1 |
| light | `controlBorder` / `surface`（非文本） | 4.41:1 |
| dark | `text` / `background` | 15.53:1 |
| dark | `text` / `surface` | 10.88:1 |
| dark | `text` / `surfaceRaised` | 7.88:1 |
| dark | `textMuted` / `background` | 9.67:1 |
| dark | `textMuted` / `surface` | 6.78:1 |
| dark | `link` / `background` | 10.95:1 |
| dark | `link` / `surface` | 7.67:1 |
| dark | `onPrimary` / `primary` | 11.20:1 |
| dark | `onPrimary` / `primaryHover` | 12.40:1 |
| dark | `onSelection` / `selection` | 6.91:1 |
| dark | `success` / `successSurface` | 7.33:1 |
| dark | `warning` / `warningSurface` | 8.26:1 |
| dark | `danger` / `dangerSurface` | 5.42:1 |
| dark | `info` / `infoSurface` | 7.02:1 |
| dark | `focus` / `background`（非文本） | 10.95:1 |
| dark | `focus` / `surface`（非文本） | 7.67:1 |
| dark | `controlBorder` / `background`（非文本） | 6.14:1 |
| dark | `controlBorder` / `surface`（非文本） | 4.30:1 |
| wearable | `text` / `background` | 18.63:1 |
| wearable | `text` / `surface` | 15.24:1 |
| wearable | `text` / `surfaceRaised` | 12.63:1 |
| wearable | `textMuted` / `background` | 11.60:1 |
| wearable | `textMuted` / `surface` | 9.49:1 |
| wearable | `link` / `background` | 13.14:1 |
| wearable | `link` / `surface` | 10.75:1 |
| wearable | `onPrimary` / `primary` | 11.20:1 |
| wearable | `onPrimary` / `primaryHover` | 12.40:1 |
| wearable | `onSelection` / `selection` | 6.91:1 |
| wearable | `success` / `successSurface` | 7.33:1 |
| wearable | `warning` / `warningSurface` | 8.26:1 |
| wearable | `danger` / `dangerSurface` | 5.42:1 |
| wearable | `info` / `infoSurface` | 7.02:1 |
| wearable | `focus` / `background`（非文本） | 13.14:1 |
| wearable | `focus` / `surface`（非文本） | 10.75:1 |
| wearable | `controlBorder` / `background`（非文本） | 7.37:1 |
| wearable | `controlBorder` / `surface`（非文本） | 6.03:1 |
| wearable | `heartRate` / `background` | 8.43:1 |
| wearable | `activity` / `background` | 16.05:1 |
| wearable | `onPrimary` / `primaryPressed` | 7.73:1 |
| aod | `time` / `background` | 11.60:1 |
| aod | `text` / `background` | 7.37:1 |
