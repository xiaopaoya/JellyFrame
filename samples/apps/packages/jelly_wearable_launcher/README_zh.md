# Wearable Launcher

> 最后更新：2026-09-09；适用版本：0.6.0-dev；Render Core 基线：0.6.2

这是一个面向 round-300 的图标优先可穿戴启动器视觉样例。紧凑的 `max-height`、
`max-width` 规则也让 app grid 可用于标准 320 x 240、172 x 320 验收 target。它不是必须的
一方系统启动器，也不会安装、删除或启动真实 app。

六个图标只使用有界 CSS 原语构成：radial/conic gradient、圆角盒、圆形和短线元素。页面不使用
JavaScript、宿主服务、图片或 Canvas，可作为低成本消费级可穿戴 app grid 的参考。

## 视觉用途

采用可穿戴配色；以下宿主能力和验收限制仍然适用。

只有静态按钮和虚构二页指示，无法启动 app；图标细节保留作绘制 fixture，不再作为展示入口。
