# 路线图

## 里程碑 1：静态文档核心

- HTML 子集：document 节点、文本、常见 block 和 inline 标签
- CSS 子集：简单选择器和少量属性
- Layout：block flow、文本测量抽象
- Rendering：稀疏 layer tree、裁剪，以及包含矩形、文本和图片占位符的 display list

状态：基本完成，并且当前 app-oriented 子集已经超过最初计划。

## 里程碑 2：嵌入式渲染后端

- 软件 framebuffer 后端：已可用于验证
- 基于 layer invalidation 的脏矩形重绘
- 字体 atlas 或平台文本后端：Win32/GDI 已用于验证；仍需要嵌入式后端
- 指针/触摸输入路由：已有 pointer/wheel 核心；仍需要可穿戴 focus/touch adapters

## 里程碑 3：App runtime

- JerryScript 集成：已有可选 scripting build
- DOM mutation APIs：已可用
- Timer/event loop：已有宿主泵动 timer
- Classic document script loading：已在 scripting 示例壳中可用
- Fetch/resource abstraction
- Device capability APIs

## 里程碑 4：可穿戴 UI 能力

- 小屏 viewport model
- 适配表冠、按键、触摸的 focus/navigation model
- 低功耗动画调度
- App 打包格式

## 推荐下一步顺序

1. Dirty rectangle invalidation 和 `HostFrameSink` presentation。
2. 可部署 embedded framebuffer backend。
3. Win32/GDI 之外的平台文本后端。
4. 面向触摸、按键和表冠的可穿戴 focus/navigation model。
5. Resource 和 device capability APIs。
