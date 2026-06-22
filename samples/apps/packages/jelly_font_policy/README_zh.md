# Jelly Font Policy

这个小 package 用来验收 app 字体策略路径：

- CSS 声明 `font-family: "Jelly Tiny", system-ui, sans-serif`。
- `jellyframe.app.json` 声明同名 family，并指向包内 `.jffont` 补充包。
- `jellyframe_cli.py check` 会把该 family 报告为 manifest runtime font，并在安装前验证 glyph 覆盖。

这里的字体故意很小，只用于确定性的 package/tool 测试。产品 app 应从有授权的 bitmap font 生成自己的
`.jffont` 子集。
