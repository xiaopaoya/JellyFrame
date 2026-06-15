# 路线图

## 里程碑 1：静态文档核心

- HTML 子集：document 节点、文本、常见 block 和 inline 标签
- CSS 子集：简单选择器和少量属性
- Layout：block flow、文本测量抽象
- Rendering：稀疏 layer tree、裁剪，以及包含矩形、文本和图片占位符的 display list

状态：基本完成，并且当前 app-oriented 子集已经超过最初计划。

## 里程碑 2：嵌入式渲染后端

- 软件 framebuffer 后端：已可用于验证
- 脏矩形重绘：第一版自动 `dirty_region` 子集已可用于非结构性 DOM 变化；仍需要更细的
  layer/display-command invalidation
- 嵌入式 framebuffer adapter：已支持调用方持有的 RGBA8888/BGRA8888、RGB565/BGR565、
  RGB332、Gray8 和单色 buffer
- 平台文本测量/绘制后端：API 已有，Win32/GDI 验证后端已接入，第一版静态 embedded bitmap
  backend 和 BDF pack 生成器已可用；仍需要 LVGL/vendor adapters
- 指针/触摸输入路由：已有 pointer/wheel 核心；button/crown focus navigation 已有第一版 core API；
  仍需要开发板 adapter
- 平台无关开发板 bring-up 形态：已通过 `wearweb_embedded_host_demo` 提供静态资源/RGB565
  demo

## 里程碑 3：App runtime

- JerryScript 集成：已有可选 scripting build
- DOM mutation APIs：已可用
- Timer/event loop：已有宿主泵动 timer
- Classic document script loading：已在 scripting 示例壳中可用
- Fetch/resource abstraction
- Device capability APIs：第一版 `HostDeviceCapabilities` 契约已可用；更深的自动适配延后

## 里程碑 4：可穿戴 UI 能力

- 小屏 viewport model
- 适配表冠、按键、触摸的 focus/navigation model
- 低功耗动画调度
- App 打包格式

## 推荐下一步顺序

1. 基于 `wearweb_embedded_host_demo` 完成具体开发板、LVGL 或显示驱动示例。
2. 在静态 bitmap backend 之外补充 LVGL/vendor text backend adapters。
3. Resource 和 device capability APIs。
4. 面向 parser、style、script 和 framebuffer memory 的集中 budgets。
5. 面向无法保留完整 target buffer 的屏幕补充 tiled framebuffer presentation。
