# JellyFrame ESP32-S3 ESP-IDF Port

> 最后更新：2026-08-09；适用版本：0.6.0-dev；兼容基线：0.5.0

这是 ESP32-S3 的硬件 bring-up 项目。它围绕 `docs/embedded_hal_api_zh.md` 接入平台无关
引擎，并负责开发板显示、触摸、任务、资源和实机验收；不要把板级优化写进 `src/`。

## 按工作内容查找

| 目标 | 入口 | 负责内容 |
| --- | --- | --- |
| 选择 profile 和构建 | `sdkconfig*.defaults`、`CMakeLists.txt` | ESP-IDF 配置和 target |
| 显示/触摸接入 | `main/boards/`、`main/jellyframe_esp32s3_hal.*`、`main/jellyframe_esp32s3_input.*` | panel、DMA、touch 生命周期 |
| 启动模式和验收 | `main/main.cpp`、`main/*acceptance.cpp` | port-owned fixture 和串口证据 |
| 静态 app 资源 | `resources/app/`、`resources/README_zh.md` | 生成前的源资源 |
| ESP-IDF 组件包装 | `components/`、`components/README.md` | CMake component 边界 |
| 资源、字体和报告工具 | `tools/`、`tools/README.md` | 主机侧生成和证据整理 |

`resources/app/` 中的页面按 smoke、shell、性能 workload、负向资源验收分类；它们不是
面向外部作者的模板。新 app 从 `../../tools/templates/apps/` 开始。

## WS147 Developer Image 发布材料

WS147 Developer Image 的不可变 identity 记录位于
[`releases/ws147-developer-image-0.1.0-dev.json`](releases/ws147-developer-image-0.1.0-dev.json)。
它将精确的 factory app 与完整 factory image SHA-256 绑定到 WS147
`rect-172x320` profile、JFDP/1 USB Serial/JTAG transport、已支持 feature family
及 327680-byte bundle 上限。写入这份 16 MB raw factory image 时只能遵循配套的
[`releases/ws147-usb-recovery-v1.md`](releases/ws147-usb-recovery-v1.md)。本发布证据
只覆盖 Developer Image recovery 和 JFDP lifecycle transport，不替代面板或触摸验收。

构建目录、生成 C++ 资源表、串口日志和实机报告均为本地产物，不进入可移植源码提交。
