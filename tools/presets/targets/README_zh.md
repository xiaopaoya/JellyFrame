# Target Presets

这些文件是 packaging 和 validation tools 使用的设备能力 preset。

请保持 preset 保守。一个 preset 应描述 app 可以稳定依赖的能力，而不是某块开发板暴露的全部
可选硬件特性。

`jellyframe_cli.py check`、`preview`、`package` 和 `install` 可以通过显式传入
`--targets` 或 `--all-targets` 运行多 target responsive validation。该流程会对同一个
package 按每个 preset 跑一次 render-core pseudo browser，并把紧凑结果写入 JSON report 的
`responsiveProfiles[]`。

Responsive profile 是桌面开发信号，不是运行时功能。它报告 viewport 尺寸、形状、content
height、横向溢出、是否需要滚动以及 diagnostics 计数，帮助 app 作者判断一个 package 在多个
可穿戴设备形态上是否仍然可用。普通单 target 命令保持旧 report 结构，不输出
`responsiveProfiles[]`。

字体预算字段（`maxAppFonts`、`maxAppFontBytes`、`maxAppFontGlyphs`）是 installable
`.jffont` 补充包的工具限制。它们应反映 flash/storage 和安装策略预期，而不是每个 app 都共享的
系统固件字体。
