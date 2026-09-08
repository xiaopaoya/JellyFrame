# JellyFrame 品牌标识

> 最后更新：2026-09-08；适用版本：0.6.0-dev

本稿依据项目现有的 `native-jelly` 视觉语言设计：浅海蓝、柔软膜面、水母与果冻。圆拱形外轮廓同时表达水母伞盖与小屏界面框架；内部留白是屏幕窗口；中间下垂的笔画形成字母 J。常规版与单色版共享轮廓。

第二次微调：在 24 单位画布中，三条触手均加长 0.75 单位，中间 J 的弯钩向左舒展 0.25 单位，增加触手占比并保留原有伞盖和配色。

## 文件

项目配色见 [可穿戴优先色卡与使用规范](PALETTE_zh.md)，包含圆形手表、窄屏运动手环与息屏表盘示例；附 [色卡预览](palette.png)、[SVG 色卡](palette.svg)、[CSS 变量](palette.css) 和 [JSON 数据](palette.json)。其他嵌入式屏幕复用同一色阶作为补充场景。

| 文件 | 用途 |
| --- | --- |
| `jellyframe-logo.svg` | 常规彩色标识，透明背景，可缩放 |
| `jellyframe-logo-512.png` | 512 × 512 透明 PNG |
| `jellyframe-logo-128.png` | 128 × 128 透明 PNG，可用于扩展列表 / Marketplace 图标 |
| `jellyframe-wordmark.svg` | 带 JellyFrame 名称的横版组合，字体已转为路径 |
| `jellyframe-vscode-mono.svg` | VS Code 活动栏单色 SVG，24 × 24，使用 currentColor |
| `jellyframe-mono-dark-512.png` | 深色单色透明 PNG，适合浅底 |
| `jellyframe-mono-light-512.png` | 白色单色透明 PNG，适合深底 |
| `preview.png` / `preview.svg` | 双版本展示及 16 / 20 / 24 / 32 px 尺寸预览 |

## 使用建议

- 彩色：浅海青 `#67DDED` → 海蓝 `#1680C7`，深蓝收尾 `#176DB1`；正文搭配 `#101820`。
- 标识四周至少保留图形宽度的 1/6 作为安全距离；不要拉伸、旋转或给单色版添加渐变。
- 单色图以 24 px 活动栏尺寸为主要目标；16 px 仅作紧凑场景参考。
- VS Code 活动栏使用单色 SVG；扩展详情 / Marketplace 的 `package.json` 顶层 `icon` 使用 PNG。
- 仓库中英文 README 和文档索引使用本目录的彩色 PNG。
- 插件活动栏使用 `tools/vscode-jellyframe/media/jellyframe.svg`，与本目录的单色 SVG 保持一致。
- 插件扩展列表、插件 README、可视化编辑器 / 调试 / 报告标签使用 `tools/vscode-jellyframe/media/jellyframe.png`，与本目录的 128 px 彩色 PNG 保持一致。两种打包方式均包含这些资源。
- 修改源标识并重新导出后，同步复制单色 SVG 和 128 px PNG 到上述插件路径。

`preview.png` 中的编辑器为图标应用示意，实际主题着色以 VS Code 为准。
