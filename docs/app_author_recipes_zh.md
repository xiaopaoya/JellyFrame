# App 组件 Recipes

> 最后更新：2026-07-22；适用版本：0.5.0

这些 recipes 是给小屏可穿戴 app 作者复制改写的起点。它们只使用 JellyFrame 文档化子集，
不依赖浏览器专有行为。

活样例位于 `samples/apps/packages/jelly_component_recipes`。

## 页面骨架

页面高度固定到目标 viewport，把可滚动内容放进一个明确的子容器。

```html
<main class="screen">
  <header class="topbar">...</header>
  <section class="content-scroll">...</section>
  <nav class="bottom-nav">...</nav>
</main>
```

```css
* {
  box-sizing: border-box;
}

.screen {
  width: 100%;
  height: 300px;
  padding: 12px;
  overflow: hidden;
}

.content-scroll {
  height: 196px;
  overflow-y: auto;
}
```

## 保留占位

可选 badge 或操作需要保留位置时使用 `visibility: hidden`；只有后续内容应补上空位时才使用
`display: none`。隐藏 wrapper 内确有必要保持可见的子元素可显式声明 `visibility: visible`。
不支持 `collapse`。

```css
.sync-slot { height: 26px; visibility: hidden; }
.sync-slot.is-ready { visibility: visible; }
```

## 包内图片背景

装饰背景或整张卡片背景可直接使用本地 BMP，不必再添加一个 `img` 节点。始终保留纯色
fallback，因为目标缺少相应 codec 或图片预算不足时，宿主可以拒绝解码。

```css
.weather-card {
  width: 100%;
  height: 92px;
  background-color: #12314a;
  background-image: url("/assets/weather-card.bmp");
  background-size: cover;
  background-position: center;
  background-repeat: no-repeat;
  border-radius: 14px;
}
```

该路径接受一张包内绝对路径图片。`background-size` 限于 `cover`、`contain` 或 `100% 100%`；
`background-position` 使用文档化的简单图片定位子集；只接受 `background-repeat: no-repeat`。
它刻意不接受远程/data URL、相对路径、平铺、多背景或任意尺寸表达式。

## 静态 SVG 图标

将简单图标源放入包内，在 HTML 或 CSS 中引用它，再以 `--rasterize-svg` 打包。输出 bundle/debug
package 中只保留生成的 BMP 和改写后的引用，因此目标端没有 SVG parser 或矢量分配成本。

```html
<img class="status-icon" src="assets/status.svg" alt="Ready">
```

```powershell
python tools\jellyframe_cli.py package `
  --root your_app `
  --report build\your_app.report.json `
  --output-bundle build\your_app.jfapp `
  --rasterize-svg --svg-raster-size 32
```

该路径用于静态的 16/24/32px 风格图标，不是 SVG UI。只能使用静态 HTML/CSS 引用。转换器接受基础图形和
简单 path，但会拒绝 SVG 文本、filter、渐变、transform、脚本、远程数据和 arc path command。检查 package
report 中的 `staticSvgRasterization`；被拒绝时应替换为预光栅化 bitmap。

## 按钮

按钮文字尽量短。默认一行一到两个按钮。窄屏优先减少按钮数量或缩短标签。

```css
.button-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 7px;
}

.button {
  width: 100%;
  height: 32px;
  border-radius: 16px;
  text-align: center;
}

/* 窄屏优先减少按钮数量或缩短标签。 */
```

## 固定设置 Grid

只有设置页确实需要稳定的操作区时才使用显式行。该有界子集支持 2-4 条固定/`1fr` 行和正整数
placement，不支持 named area 或完整浏览器 Grid。

```css
.settings-grid {
  display: grid;
  height: 156px;
  grid-template-columns: 56px 1fr;
  grid-template-rows: 30px 1fr 34px;
  gap: 7px;
}

.settings-title { grid-column: 1 / 3; grid-row: 1; }
.settings-label { grid-column: 1; grid-row: 2; }
.settings-value { grid-column: 2; grid-row: 2; }
.settings-save { grid-column: 1 / 3; grid-row: 3; }
```

## 紧凑标签

短标签可使用字距；可能没有空格的数据可使用 scalar-safe 换行。两者都不能替代具备 shaping 的
字体后端。

```css
.eyebrow { letter-spacing: 1px; }
.device-name { overflow-wrap: anywhere; }
```

## 卡片

卡片用于重复数据项或控件组。不要把卡片再塞进卡片。

```css
.card {
  max-width: 100%;
  padding: 11px;
  border: 1px solid rgba(144, 237, 236, 0.64);
  border-radius: 18px;
  overflow: hidden;
}
```

## 水凝胶表面

用一层基础渐变和一层透明径向高光建立体积感。这是两层背景，应只用于主卡片等重点区域，
不要铺满列表。阴影有明确的栅格边界，且只有声明它的元素才会产生绘制工作。

```css
.gel-card {
  background:
    radial-gradient(circle at 80% 12%, rgba(241, 253, 255, 0.22) 0%, transparent 100%),
    linear-gradient(135deg, #315a7a, #142331);
  border: 1px solid color-mix(in srgb, #b7edff 18%, #315a7a);
  border-radius: 16px;
  box-shadow: 0 6px 10px 1px color-mix(in srgb, rgba(0, 0, 0, 0.42) 76%, rgba(98, 223, 247, 0.26));
}
```

## 状态页

空状态、离线或错误状态使用居中的状态卡片。标题要短，只暴露一个主操作。

```html
<article class="status-card">
  <span class="status-icon">!</span>
  <h2>Offline</h2>
  <p>Keep failure states short.</p>
  <button class="button primary">Retry</button>
</article>
```

```css
.status-card {
  min-height: 118px;
  text-align: center;
}

.status-icon {
  width: 34px;
  height: 34px;
  margin: 0 auto 7px;
  border-radius: 50%;
}
```

## 控制面板

控制面板使用短标签和固定高度行。窄屏不要把很多大控件横向挤在一起。

```html
<div class="control-row">
  <span>Bright</span>
  <div class="mini-meter"><span></span></div>
</div>
```

```css
.control-row {
  display: grid;
  grid-template-columns: 58px 1fr;
  gap: 8px;
  align-items: center;
  height: 30px;
}
```

## 滚动列表

内容可能超过 viewport 时，使用固定高度滚动区域。固定底部导航放在滚动区域外。

```css
.content-scroll {
  height: 196px;
  overflow-y: auto;
}

.bottom-nav {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 7px;
  height: 34px;
}
```

只有当滚动是设计的一部分时，才在 manifest target gate 中设置 `allowScroll: true`。

每个 route 保持一个主纵向滚动区域。如果内部卡片也需要移动，把内容放进外层列表，或改为
不溢出的固定高度控件。两个实际溢出的滚动容器嵌套时会竞争触控拖动输入；`check` 会报告
`visual-nested-scroll-container`，并给出内外两个 DOM path。

## 本地表单流程

设置、配网和账号类流程使用本地 `submit` 事件：页面内维护表单状态、先在本地校验，再由
JavaScript 调用已获准的宿主服务。不要设置 `action` 或 `method`：JellyFrame 不会导航、编码
multipart payload，也不会执行浏览器 form POST。

```html
<form id="wifi-form">
  <input name="network" required maxlength="32">
  <input name="password" required minlength="8" maxlength="63">
  <button type="submit">Connect</button>
</form>
```

```js
const form = document.getElementById("wifi-form");
form.addEventListener("submit", function (event) {
  event.preventDefault();
  const fields = new FormData(form);
  // 在这里调用文档化、受 capability gate 约束的宿主服务。
  // 不要期待页面跳转或自动网络请求。
});
```

`form.checkValidity()` 和 `form.reportValidity()` 返回布尔值，并向每个无效控件派发不冒泡的
`invalid`。没有浏览器校验弹窗。`FormData` 只保存字符串 entry，支持 `append`、`set`、
`delete`、`get`、`getAll`、`has` 和 `forEach`。`forEach` 按 entry 顺序传入
`(value, name, formData)`；回调中新加的 entry 有意留到下一次调用，从而避免一次有界 app
回调持续扩大当前遍历。

## 确认对话框

删除数据或需要用户确认的操作使用一个 modal dialog。由发起控件打开，再由 dialog 内的操作显式关闭。
宿主可以把 Escape 或硬件 Back 映射为 `cancel`；只有 app 必须继续显示确认时才调用 `preventDefault()`。

```html
<button id="clear-data">清除数据</button>
<dialog id="confirm-clear">
  <p>清除本地数据？</p>
  <div class="button-row">
    <button id="keep-data" type="button">保留</button>
    <button id="confirm-data" type="button">清除</button>
  </div>
</dialog>
```

```js
const dialog = document.getElementById("confirm-clear");
document.getElementById("clear-data").addEventListener("click", function () {
  dialog.showModal();
});
document.getElementById("keep-data").addEventListener("click", function () {
  dialog.close("keep");
});
document.getElementById("confirm-data").addEventListener("click", function () {
  dialog.close("clear");
});
dialog.addEventListener("close", function () {
  if (dialog.returnValue === "clear") {
    // 启动文档化的宿主清数据操作。
  }
});
```

一个 document 同时只能有一个 `showModal()` dialog。没有嵌套 modal、click-outside close、浏览器
backdrop、`show()` 或 `requestClose()`。

## 静态本地模块

较大的 app 可以在源码中使用一个静态本地 module 入口，同时让设备 runtime 继续执行文档化的
classic-script 路径。打包会把 module 标签替换为一个生成的 classic bundle。

```html
<script type="module" src="scripts/app.js"></script>
```

```js
// scripts/app.js
import { formatMinutes } from "./time.js";
document.getElementById("value").textContent = formatMinutes(42);

// scripts/time.js
export function formatMinutes(value) {
  return value + "m";
}
```

只使用 package-local `.js` 文件，保持一个 module entry 和无环的静态图。动态 `import()`、远程
module、`modulepreload`、`export *`、re-export declaration 和 inline module script 会被拒绝或延后。
`runtime.script` 仍应写为 `"classic"`：它描述生成后的设备 payload，而不是源码 authoring 形式。

## App 内路由

小型设置流或 tab 集合可使用 fragment route，它不会更新 host URL，也不会创建浏览器导航 history：

```js
function renderRoute() {
  var route = location.hash || "#home";
  document.getElementById("page").textContent = route.slice(1);
}

window.addEventListener("hashchange", renderRoute);
location.hash = "settings";
renderRoute();
```

支持面包括 `location.hash`、`hashchange`、`popstate`、`onhashchange`、
`onpopstate` 与只处理 fragment 的 `history`。`history.back()`、`forward()`、
`go(delta)`、`pushState()`、`replaceState()` 会保留有界 `#fragment` entry；
`state` 与 `title` 不保留。`location.assign()`、远程导航和跨 app 路由均不可用。
完整 package 参见 `jelly_route_tabs`。

## 窄屏目标

对 `172x320`，先减少横向决策：

- 优先改成纵向 stack。
- 数值和标签保持短。
- 先缩 padding，再缩可读文本。
- 避免一行放三个以上宽按钮。

## 圆屏优先首屏

对 `round-300`，把边缘区域留给背景和低优先级内容。最重要的卡片或状态放在视觉中心，
底部导航保持紧凑，并始终用同一个 package 验证 `rect-320x240` 和 `rect-172x320`。

## 验收

分享 package 前运行：

```powershell
python tools\jellyframe_cli.py check `
  --root your_app `
  --target round-300 `
  --targets round-300,rect-320x240,rect-172x320 `
  --report build\your_app.report.json `
  --build-dir build\Release
```

然后检查 `developerAdvice[]` 和每个 target gate 的 decision。
