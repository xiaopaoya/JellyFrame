# 嵌入式应用子集

WearWeb 现在已经可以支撑按照裁剪子集编写的小型嵌入式 UI 应用。它还不是通用网页运行时，
但当前核心已经足以开发天气面板、设置页、计算器等本地应用，并且不需要把 UI 全部退回到
canvas 手绘模型。

## M6 后的实际状态

现在可行：

- 天气或仪表盘应用：数据由宿主提供，页面使用 select、button 和文本更新。
- 卡片/仪表盘布局：可以使用已支持的响应式 grid 子集、gap 和 aspect-ratio
  媒体占位。
- 计算器类工具：按钮、输入框状态和同步事件处理已经足够。
- 设置面板：可以使用 text input、textarea、checkbox、radio、range 和 select。
- 静态或半静态信息页：可以拥有现代但克制的视觉层次。

- 时钟应用可以在宿主泵动 timer 时自动更新。
- 计时器/秒表可以基于 `setInterval` 运行，由宿主决定 tick 精度和 callback budget。

暂不适合：

- 假设完整浏览器加载、网络、存储、canvas、Web Components、模块系统或大型选择器 API 的应用。
- 依赖完整 flex/grid、container query、复杂 CSSOM、DOM Range、layout observer
  或异步浏览器任务语义的大型现代网页。

## 推荐写法

HTML：

- 使用普通文档结构：heading、section、paragraph、label、button、input、textarea 和 select。
- 交互节点优先使用稳定的 `id`。
- CSS 可来自本地文件、内嵌 `<style>`，或由壳层提供本地 linked stylesheet 加载。
- 对复杂媒体和高级组件保持可降级。

CSS：

- 使用简单 selector：tag、class、id、descendant、child 和简单 attribute selector。
- 使用 block、inline-block、简化 inline flow、基础 flex、margin、padding、border、颜色、字号、行高和文本对齐。
- 在适合卡片 UI 时使用响应式 grid 子集：`display:grid`、
  `repeat(auto-fit, minmax(<length>, 1fr))`、`gap`、
  `grid-auto-rows: minmax(<length>, auto)`，以及 `grid-column`/`grid-row:
  span N`。
- 使用 `aspect-ratio` 创建媒体占位和视觉面板。
- 可以做现代配色、留白和层次，但不要依赖完整浏览器 grid/flexbox 行为、
  container query、subgrid 或 dense packing。
- 控件样式保持简单。引擎会绘制轻量原生控件外观，并在复杂效果被丢弃时保留可用性。

JavaScript：

- 现阶段使用宿主显式加载的 classic script。
- 使用 `document.getElementById`，不要依赖 selector API。
- 已支持 DOM 操作：`createElement`、`createTextNode`、`appendChild`、`removeChild`、
  `setAttribute`、`getAttribute` 和 `textContent`。
- 已支持事件：`addEventListener`、`removeEventListener`、捕获/目标/冒泡、`click`、
  `input`、`change`，以及鼠标和滚轮字段。
- 已支持相关控件上的 `value`、`checked` 和 `selectedIndex`。
- 应用逻辑仍应以同步为主。Timer 已可用，但 promise job pump 和浏览器加载任务仍不在子集内。

## 开发者学习成本

如果子集文档写清楚，学习成本是可以接受的。它仍然像小型 DOM 应用：HTML 描述结构，
CSS 描述视觉，JavaScript 修改具名节点。

和普通浏览器开发相比，主要区别是不能假设隐式浏览器服务：

- 暂无自动 `<script>` 加载；
- `getElementById` 之外暂无 selector API；
- 暂无网络、存储和模块加载；
- 不能假设完整 DOM 框架运行环境。

这是实际约束，但比要求应用开发者手写全部 canvas UI 要温和得多。

## 示例应用

`examples/app_cases` 目录包含四个验收型应用：

- `weather.*`：select 驱动的天气面板，带温度单位切换。
- `clock.*`：通过 M6 `setInterval` 自动刷新的时钟展示。
- `timer.*`：使用 M6 `setInterval` 的有状态计时器。
- `calculator.*`：基于按钮、`input.value` 和事件 listener 的计算器。

通过 scripting pseudo browser 运行：

```powershell
.\build-script\Release\wearweb_pseudo_browser.exe examples\app_cases\weather.html examples\app_cases\weather.css weather.bmp 360 360 --script examples\app_cases\weather.js
.\build-script\Release\wearweb_pseudo_browser.exe examples\app_cases\clock.html examples\app_cases\clock.css clock.bmp 360 360 --script examples\app_cases\clock.js --pump-timers 3200
```

通过 Win32 壳交互运行：

```powershell
.\build-script\Release\wearweb_win32_browser.exe examples\app_cases\calculator.html examples\app_cases\calculator.css --script examples\app_cases\calculator.js
```

## 是否可以进入 M7

可以进入 M7。当前高收益缺口已经不再是 timer，而是脚本加载的人体工学。
M7 应支持 inline classic `<script>` 和壳层提供的本地脚本加载 callback，同时继续把网络加载和模块系统留在嵌入式核心之外。
