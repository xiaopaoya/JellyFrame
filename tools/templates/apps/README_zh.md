# 应用模板

常见可穿戴工作流的参考应用模板。这些模板刻意保持小、现代且品牌中性的手表式 UI，
并停留在 JellyFrame 已文档化的 HTML、CSS 和脚本子集内，而不是依赖完整浏览器布局行为，
也不复刻任何商业手表界面。

- `calculator/`：紧凑 quick-math 键盘、事件委托、`dataset` 和本地状态。
- `clock/`：timer 驱动的 dayline 显示更新和紧凑健康指标。
- `timer/`：控制按钮、状态变化和时间格式化。
- `weather/`：面向未来宿主网络 API 的数据型 UI。

这些目录是 app 作者的起点。它们会保持正常 source-package 结构，但不承载每个边缘测试；
用于验收和展示的完整示例放在 `../../../samples/apps/packages`，更小的针对性 fixture 放在
`../../../samples/apps/loose`、`../../../src/render_core/samples/pages/modern` 和 `../../../src/script/samples/classic`。
