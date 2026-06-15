# Layer Tree 裁剪范围

日期：2026-06-14

WHATWG 没有规定浏览器的 layer tree。这里属于浏览器实现层，因此
JellyFrame 参考现代内核的事实结构，同时为了 framebuffer 级别硬件做深度裁剪。

参考点：

- Blink/Chromium 将 layout、paint chunks、paint property trees 和 compositing
  拆分为多个阶段。参见 Chromium RenderingNG 架构说明：
  https://developer.chrome.com/docs/chromium/renderingng-architecture
- WebKit 长期围绕 rendering objects 使用 `RenderLayer` 和 compositing layers：
  https://github.com/WebKit/WebKit/tree/main/Source/WebCore/rendering
- Gecko/WebRender 在发送 retained scene 数据给 renderer 前消费 display lists
  和 stacking contexts：
  https://firefox-source-docs.mozilla.org/gfx/webrender/

## JellyFrame 模型

```text
LayoutBox tree
  -> LayerTreeBuilder
  -> LayerNode tree
  -> flattened DisplayList
  -> platform renderer
```

Layer tree 是一种可保留的绘制组织结构。它不是 GPU API，也不要求纹理。
平台后端可以把它 flatten 成矩形和文本命令；未来也可以把部分 layer 映射到硬件 surface。

## 已实现的成层原因

- 根文档 layer。
- `overflow: hidden`、`overflow: clip`、`overflow: auto` 和 `overflow: scroll`
  创建裁剪 layer。
- 小于 1 的 `opacity` 创建 composited layer，并在 flatten 时应用透明度。
- `transform` 创建 composited layer，但暂不执行矩阵变换。
- `position` 和显式 `z-index` 创建 stacking layer。
- `box-shadow` 和圆角 overflow clip 会创建 layer 边界。当前绘制会输出便宜的圆角半透明阴影填充；
  未来可替换为真实 blur 而不改变树结构。

## 降级策略

- 不支持的 transform 值不会导致崩溃，也不会错误移动盒子；当前只作为合成边界信号。
- 已检测圆角裁剪，但当前近似为矩形裁剪。
- `box-shadow` blur 近似为矩形扩张和半透明填充。
- `z-index` 排序目前是 layer-local 的，对 positioned children 有用，但还不是完整 CSS stacking-context 算法。
- Layer tree 始终可以 flatten 为简单 `DisplayList`，保留低端 renderer 路径。

## 嵌入式约束

- 稀疏成层。普通盒子绘制进父 layer。
- 只对子 layer 排序，不对所有 layout boxes 做全局排序。
- 矩形裁剪使用整数坐标，保持低分配。
- 该模型为未来 dirty-rectangle repaint 预留空间，可以失效一个 layer subtree，而不是重绘整个文档。

## 延后

- 完整 CSS stacking-context 算法。
- Transform 矩阵和变换后的裁剪。
- Filters、backdrop filters 和 blend modes。
- Retained display-list diffing。
- 纹理分配和 GPU compositing。
