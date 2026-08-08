# 外部开发者试用准入清单

> 最后更新：2026-08-09；适用版本：0.6.0-dev

这是小规模外部开发者试用的准入清单，不是完整浏览器兼容或硬件零缺陷承诺。
只有指定命令能在干净构建目录复现，并且证据满足要求，门项才算打开。

## 作者流程

1. 先读 [HOW_TO_START_zh.md](../HOW_TO_START_zh.md)、[app_author_guide_zh.md](app_author_guide_zh.md)、
   能力速查表和可搜索的 HTML/CSS 全表，再选择语法特性。
2. 配置并构建桌面 Release shell，先运行 `ctest`，避免把工具链失败误判为 app 问题。
3. 用 `jellyframe_cli.py new` 创建 source package，只编辑本地 HTML、CSS、classic 或 package-time
   static-module JavaScript，以及 manifest 中声明的资源。
4. 执行 `jellyframe_cli.py check --targets round-300,rect-320x240,rect-172x320`，修复所有 `error`，
   并逐项检查 `warning`、`developerAdvice[]`、`performanceAdvice[]`、字体和 target gate 结果。
5. 为目标 profile 执行 `preview`，再用 Win32 壳和确定性 frame script 检查输入、滚动和动画。
6. 用 `package` 生成 `.jfapp`，检查报告并安装到桌面 registry；用 `doctor` 做仓库样例总检，
   用 `trial` 做严格的官方试用证据流程。
7. 桌面报告干净后，再请移植侧执行目标 profile。硬件报告必须分开记录核心渲染、格式转换、present/DMA、
   内存水位、输入到达和目检结果。

## 准入门项

| 门项 | 必需证据 | 阻塞问题 |
| --- | --- | --- |
| 构建 | 干净 Release、Debug 配置和构建 | 依赖缺失、生成 profile 过期或链接失败 |
| 回归 | 相关 CTest 与 `doctor --trial` | 任意非预期失败或未分类 warning |
| 编写 | 干净目录完成 `new -> edit -> check` | 命令/路径不一致，或报告无法指出修复方向 |
| 渲染 | 三个 target preview 与 Win32 capture | 文本溢出、裁剪错误、fallback 破坏、输入/滚动失败 |
| 打包 | `.jfapp` 报告、安装和启动 | 资源越界、manifest 漂移、安装/更新非原子 |
| 恢复 | 坏 app、回滚、删除流程 | 启动器崩溃、registry 残留或确认外数据丢失 |
| 实机 | 移植侧 profile、帧率和内存证据 | reset、watchdog、flush 失败、无法解释的视觉/输入问题 |

## 明确范围

支持契约以能力矩阵为准，不以浏览器直觉推断。当前高价值作者能力包括有界 block/inline、flex/grid、
表单、本地 route、classic script、package-time static module、transition/keyframe、圆角、渐变、
阴影、文本溢出控制、包内图片和 opt-in Canvas 2D。完整 Worker、Shadow DOM、iframe、浏览器存储、
远程加载、完整媒体、SVG 和浏览器级复杂文本 shaping 仍是 partial、host-dependent 或 deferred。

`minJellyFrame: 0.5.0` 是 package 兼容基线。不能因为仓库进入 0.6 开发线就改成 `0.6.0-dev`。

## 证据规则

- 桌面 preview 只能证明桌面管线行为。
- 移植 benchmark 必须记录 workload、target、构建/profile、frame/present p50/p95、dirty area、
  conversion/DMA 时间、内存最低水位和失败计数；不能用平均值或极值推导缺失的 p95。
- 能力状态变更必须同时有实现、行为回归、文档和对应 target gate。
- 原始移植日志和报告不进入 source commit；需要时可在本地 `project_docs/` 整理发布证据，
  对外文档只引用结论和证据范围。

## 暂停试用条件

干净样例无法打包或启动、诊断无法定位、文档声称支持的能力静默消失、恢复后 registry/数据状态不明确，
或实机出现 reset/watchdog/present 错误时，暂停招募。记录最小复现，并先判断是 core、tool、文档、host
还是 port 问题，再改变主线范围。
