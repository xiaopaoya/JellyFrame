# App 模板约定

> 最后更新：2026-08-26；适用版本：0.6.0-dev；Render Core 基线：0.6.1

模板是刻意精简的作者起点，不是能力 fixture。请保持以下结构一致，让新建 package
一眼可读：

- `jellyframe.app.json` 是唯一 manifest；新增产品 permission/capability 前，保留 schema、
  当前 Runtime/Core 最低版本、entry 路径和一个明确 target。
- `index.html` 以 `<!doctype html>` 开始，声明 `lang="en"`，链接一个本地 stylesheet，
  包含一个语义化 `<main>` 根节点，并在 `<body>` 末尾加载一个本地 classic script。
- `styles/app.css` 只负责表现。使用已文档化子集；round-300 模板保留明确的固定设计视口。
- `scripts/app.js` 只负责本地交互与状态。不要把宿主私有全局、私有 shell API 或第二套模块加载器
  放进起始模板。

学习某个高级能力时，请使用展示包或针对性验收包。不要把 starter 变成所有历史子系统的记录。
