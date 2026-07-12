# App 组件 Recipes

> 最后更新：2026-07-12；适用版本：0.5.0-dev

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
`delete`、`get`、`getAll` 和 `has`。

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

小型设置流或 tab 集合可使用 fragment route，它不会更新 host URL，也不会创建 browser history：

```js
function renderRoute() {
  var route = location.hash || "#home";
  document.getElementById("page").textContent = route.slice(1);
}

window.addEventListener("hashchange", renderRoute);
location.hash = "settings";
renderRoute();
```

支持面只有 `location.hash`、`hashchange` 和 `onhashchange`。`history`、
`location.assign()`、远程导航和跨 app 路由均不可用。完整 package 参见
`jelly_route_tabs`。

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
