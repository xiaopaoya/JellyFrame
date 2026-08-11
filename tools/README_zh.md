# Tools 工具目录

> 最后更新：2026-08-09；适用版本：0.6.0-dev；兼容基线：0.5.0

这里是桌面端 app 打包、检查、验收和编辑器集成工具。先按目标找入口：

| 目标 | 从这里开始 | 目标人群 |
| --- | --- | --- |
| 创建、检查、预览和打包 app | `jellyframe_cli.py`、`package_app.py`、`templates/` | App 作者 |
| 查看像素、布局、事件和帧脚本 | `native/README_zh.md` | App 作者、UI 审阅者 |
| 安装、更新、回滚和启动器恢复 | `jellyframe_cli.py`、`app_registry.py`、`schemas/` | Host/runtime 开发者 |
| 检查 Render Core profile、链接归属和桌面性能 | `render_core_feature_registry.py`、`check_render_core_link_map.py`、`benchmark_guard.py` | Render Core 维护者 |
| 刷新 HTML/CSS 能力审计表 | `generate_*_support_table.py`、`import_css_support_crosswork.py` | 兼容性维护者 |
| 在 VS Code 中工作 | `vscode-jellyframe/README_zh.md` | App 作者、扩展维护者 |
| 验证开发板 | `../ports/<port>/README.md`、`../docs/porting_work_guide_zh.md` | 移植维护者 |

常用流程是：

```powershell
python tools\jellyframe_cli.py new --template weather --output build\my_app
python tools\jellyframe_cli.py check --root build\my_app --all-targets --build-dir build\desktop-release\Release
python tools\jellyframe_cli.py preview --root build\my_app --target round-300 --build-dir build\desktop-release\Release --output build\my_app.bmp
python tools\jellyframe_cli.py package --root build\my_app --report build\my_app.report.json --output-bundle build\my_app.jfapp --build-dir build\desktop-release\Release
```

`native/` 是桌面检查壳和 dump 工具；它们只能证明桌面管线行为。面板、DMA、MCU
时序和实机字体后端仍由 `ports/` 负责。嵌入式运行时不依赖本目录工具。
