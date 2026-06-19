# Events 与 Hit Testing 裁剪范围


JellyFrame 将输入接线和核心引擎分开：

- 核心引擎负责 hit testing、event objects、listener storage 和 event dispatch。
- 平台壳负责把原生输入，例如 Win32 messages 或 LVGL input callbacks，转换成核心事件。

这样可嵌入核心不会依赖 Windows、MCU GPIO、触摸控制器或 UI driver libraries。

## 参考标准

- WHATWG DOM Standard：`Event`、`EventTarget`、event path construction、capturing、target 和 bubbling phases。  
  https://dom.spec.whatwg.org/
- W3C UI Events：`MouseEvent`、`WheelEvent` 和传统 mouse event 概念。  
  https://www.w3.org/TR/uievents/
- CSSOM View：`elementFromPoint()` 是最接近 viewport 坐标 hit testing 的 Web 暴露模型。  
  https://www.w3.org/TR/cssom-view-1/
- Pointer Events 和 Touch Events 保留为后续集成点。

## 已实现

- DOM `Node` 上的 `EventTarget`。
- listener storage 惰性分配，没有 listener 的节点不分配 listener table。
- 带 listener id 的 `add_event_listener()` 和 `remove_event_listener()`。
- `Event`、`MouseEvent` 和 `WheelEvent` 数据对象。
- 平台无关 `InputController`，用于 pointer move/down/up 和 wheel input。
- `prevent_default()`、`stop_propagation()` 和 `stop_immediate_propagation()`。
- DOM event dispatch：
  - capture phase
  - target phase
  - bubble phase
- 感知 layer 的 hit testing：
  - 反向 paint/layer 顺序
  - 使用现有 layer tree 的 z-index layer ordering
  - overflow clipping
  - 命中文本节点时归一化为最近的 element target
- 输入合成：`mouseover`、`mouseout`、`mousemove`、`mousedown`、`mouseup`、`click` 和 `wheel`。
- 核心 input controller 内部追踪 hover、active 和 focus state。
- Windows 验证壳会把 Win32 mouse/wheel messages 转换为 `InputController` 调用。
- Windows 壳在 wheel dispatch 后执行简单 viewport scroll 默认行为。核心仍保持平台无关，不持有 OS scroll state。

## 明确裁剪

- 暂无 JavaScript callbacks。JerryScript 集成前先使用 C++ callbacks。
- 不支持 shadow DOM、slots 或 composed paths。
- 不支持 form、anchor、editing 或 selection 默认行为。
- 不支持 `pointer-events` CSS 属性。
- 不支持 touch 或 pointer capture。
- 不支持 transform 后坐标 hit testing。
- 暂无 keyboard dispatch。
- 输入状态变化会为受支持的动态 pseudo-classes 标记 style/layout dirty。脚本/事件产生的
  DOM mutation 使用普通 DOM dirty flags。

## 下一步

1. 添加 keyboard event objects 和 dispatch。
2. 继续细化动态 pseudo-class 变化中的 paint-only dirty rectangles。
3. 添加 event callbacks 后的 invalidation/re-render scheduling。
4. 添加 anchors、buttons、forms 和 editable controls 的 default-action hooks。
5. 后续将 event listeners 桥接到 JerryScript functions。
