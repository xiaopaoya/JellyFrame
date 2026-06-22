# 版本规则

JellyFrame Engine 使用轻量语义化版本规则：

```text
MAJOR.MINOR.PATCH[-stage]
```

## 规则

- `MAJOR`：运行时或公开 API 出现不兼容变更。
- `MINOR`：新增引擎能力，并保持已有 app 兼容。
- `PATCH`：bug 修复、parser/layout 正确性修复，以及纯文档维护。
- `-dev`：稳定 tag 发布前的活跃开发阶段。

## 发布期望

- 当前源码版本记录在 `VERSION`。
- 面向用户可见的变更记录在 `CHANGELOG.md` 和 `CHANGELOG_zh.md`。
- 公开文档提供英文和中文版本。中文文件使用 `_zh` 后缀。
- 授权条款以 `LICENSE`、`COMMERCIAL.md` 和 README 的授权说明为准。
- 早期版本预计保持小步、里程碑式发布。

## 早期版本映射

- `0.1.x`：静态 HTML/CSS 文档核心。
- `0.2.x`：framebuffer renderer 和输入路由。
- `0.3.x`：可穿戴 app runtime 开发线，包括可选 JerryScript、DOM mutation APIs、
  packaging、文本/字体工作流和嵌入式内存优化。
- `0.4.x`：在更多真实设备验证后，继续稳定 package app API。
