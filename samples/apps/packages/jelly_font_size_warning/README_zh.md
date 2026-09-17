# 字体字号安装提示探针

这个验收包只用于稳定触发 VS Code 插件的安装前字体字号提示。

- 包内 `.jffont` 的原生行高为 `8px`。
- 当前 app-font runtime 只能按 `1` 至 `8` 倍整数缩放，因此它能精确提供
  `8/16/24/32/40/48/56/64px`。
- manifest 声明并且 CSS 实际请求 `12px`，运行时会选择 `16px`，所以打包报告必须包含且仅包含一条
  `font-size-unavailable` warning。
- package 同时声明 `172x320`、`300x300` 和 `320x240` target，插件可按已连接设备的显示尺寸唯一选择。

## 插件测试

1. 在 VS Code 中打开本目录，或在 JellyFrame 侧栏选中这个 App。
2. 连接并选择设备。
3. 执行“打包并部署当前 App”。
4. 确认第一次普通部署提示后，插件应显示第二个模态提示：
   `App 字号声明不完整，或字体包无法精确提供所用字号（12px）`。
5. 选择“查看报告”应停止安装并打开报告；再次执行并选择“仍然安装”才会进入设备安装。

无需连接设备时，可先验证报告：

```powershell
python tools\package_app.py `
  --root samples\apps\packages\jelly_font_size_warning `
  --target rect-172x320 `
  --report build\font-size-warning.report.json `
  --output-bundle build\font-size-warning.jfapp
```

报告中的 `warnings[0].code` 应为 `font-size-unavailable`，`sizes` 应为 `[12]`。
