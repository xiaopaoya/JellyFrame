# 文本后端迁移

> 最后更新：2026-11-15；适用版本：0.6.0-dev

规范接口是 Core `TextSystem`：移植者提供离线准备的不可变 JFFONT2 目录及寿命，
Core 统一负责字体选择、原始度量、Q1000 缩放、字形准备和软件绘制。
不新增宿主测量、字体度量或绘制 callback。AppFontSet 已移除旧格式加载与回调。

现存 `TextMeasureProvider` / `TextPainter` 及 range/family/additive 回调仅服务尚未迁移的
生成式 BitmapFont/GDI 默认路径和测试。待生产消费者、生成/控件文本及测试一起迁移后集中删除，
不是新移植的目标接口。HostTextAdapter 包装层已移除，不应继续使用旧示例代码。

目录接入与所有权见 [Core Font Catalog](../../app_runtime/docs/core_font_catalog.md)；
剩余消费者见[退役清单](../../../docs/render_core_text_consumer_retirement.md)。
不要用平台字体 API 或 LVGL 测量/绘制替代 Core 文本语义。
