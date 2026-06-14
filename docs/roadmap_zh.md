# 路线图

## 里程碑 1：静态文档核心

- HTML 子集：document 节点、文本、常见 block 和 inline 标签
- CSS 子集：简单选择器和少量属性
- Layout：block flow、文本测量抽象
- Rendering：稀疏 layer tree、裁剪，以及包含矩形、文本和图片占位符的 display list

## 里程碑 2：嵌入式渲染后端

- 软件 framebuffer 后端
- 基于 layer invalidation 的脏矩形重绘
- 字体 atlas 或平台文本后端
- 指针/触摸输入路由

## 里程碑 3：App runtime

- JerryScript 集成
- DOM mutation APIs
- Timer/event loop
- Fetch/resource abstraction
- Device capability APIs

## 里程碑 4：可穿戴 UI 能力

- 小屏 viewport model
- 适配表冠、按键、触摸的 focus/navigation model
- 低功耗动画调度
- App 打包格式
