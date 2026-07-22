# 版本规则

> 最后更新：2026-07-23；适用版本：0.5.0-dev

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
- 公开 Markdown 文档顶部带一行轻量新鲜度信息：
  `最后更新：YYYY-MM-DD；适用版本：VERSION`。当文档的契约、示例或操作说明变化时更新这行。
  活跃开发时文档可以短暂晚于代码，但过久未更新的文件应当一眼可见。
- 授权条款以 `LICENSE`、`COMMERCIAL.md` 和 README 的授权说明为准。
- 早期版本预计保持小步、里程碑式发布。

## 早期版本映射

- `0.1.x`：静态 HTML/CSS 文档核心。
- `0.2.x`：framebuffer renderer 和输入路由。
- `0.3.x`：可穿戴 app runtime 开发线，包括可选 JerryScript、DOM mutation APIs、
  packaging、文本/字体工作流和嵌入式内存优化。
- `0.4.x`：面向可安装 package app 的 runtime 稳定化，包括管线 diagnostics、
  responsive target report、有界动画、宿主服务策略、font-family 选择和 Win32 验证工具。
- `0.5.x`：设备可用性阶段，包括 storage lifecycle 接入、retained rendering 分片、
  产品级 image codec adapter、system shell recovery 和更多实机验证。
- `0.6.x`：外部开发者试用阶段。只有完成 0.5 的设备可用性、诊断与宿主契约关闭条件后才进入；
  重点是试用反馈、分发语义和目标设备证据，不表示转向完整浏览器兼容。
- `0.7.x` 至 `1.0`：公开契约冻结阶段。manifest、capability、target gate、诊断码和
  host-service 错误语义在此期间只允许兼容性修复或有明确迁移路径的变更。
