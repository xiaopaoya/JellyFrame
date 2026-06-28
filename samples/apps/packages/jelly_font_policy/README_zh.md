# Jelly Font Policy

这个小 package 用来验收 app 字体策略路径：

- CSS 声明两个包内 family：`Jelly Tiny CN` 和 `Jelly Tiny Symbols`。
- `jellyframe.app.json` 声明两个 family，并指向包内 `.jffont` 补充包。
- `jellyframe_cli.py check` 会把两个 family 报告为 manifest runtime font，在安装前验证 glyph 覆盖，
  并故意对缺失的 `あ` 探针给出 warning。
- Win32 可用 `--use-app-fonts` 验证 runtime 文本路径：

```powershell
.\build-script\Release\jellyframe_win32_browser.exe --app samples\apps\packages\jelly_font_policy --use-app-fonts
```

这里的字体故意很小，只用于确定性的 package/tool 测试。产品 app 应从有授权的 bitmap font 生成自己的
`.jffont` 子集。
