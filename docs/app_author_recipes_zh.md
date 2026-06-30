# App 组件 Recipes

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
  overflow: auto;
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
  overflow: auto;
}

.bottom-nav {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 7px;
  height: 34px;
}
```

只有当滚动是设计的一部分时，才在 manifest target gate 中设置 `allowScroll: true`。

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
