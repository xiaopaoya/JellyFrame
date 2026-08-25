# 应用模板

> 最后更新：2026-08-25；适用版本：0.6.0-dev；兼容基线：0.5.0

常见可穿戴工作流的参考应用模板。这些模板刻意保持小、现代且品牌中性的手表式 UI，
并停留在 JellyFrame 已文档化的 HTML、CSS 和脚本子集内，而不是依赖完整浏览器布局行为，
也不复刻任何商业手表界面。

- `calculator/`：紧凑 quick-math 键盘、事件委托、`dataset` 和本地状态。
- `blank/`：最小 `Hello world` 包，带空 CSS 和 JavaScript 入口。
- `clock/`：timer 驱动的 dayline 显示更新和紧凑健康指标。
- `timer/`：控制按钮、状态变化和时间格式化。
- `weather/`：面向未来宿主网络 API 的数据型 UI。

## 按起点选择

| 模板 | 适合开始做什么 | 主要内容 |
| --- | --- | --- |
| `blank/` | 从零开始 | 最小 manifest、`Hello world` 和空 CSS/JS 入口 |
| `weather/` | 数据卡片和状态面板 | 包内图片、状态型内容 |
| `clock/` | 时间/状态 dashboard | 紧凑排版和宿主时间更新 |
| `timer/` | 交互控制循环 | 按钮、本地状态和确定性时间 |
| `calculator/` | 密集键盘布局 | 行列布局、事件委托和有界状态 |

使用 `python tools/jellyframe_cli.py new --template NAME` 复制，然后先运行 `check`。
这些是起始模板，不是完整特性 fixture，也不证明目标面板性能。

这些目录是 app 作者的起点。它们会保持正常 source-package 结构，但不承载每个边缘测试；
用于验收和展示的完整示例放在 `../../../samples/apps/packages`，更小的针对性 fixture 放在
`../../../samples/apps/loose`、`../../../src/render_core/samples/pages/modern` 和 `../../../src/script/samples/classic`。
