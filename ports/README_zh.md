# Port 与开发板接入

> 最后更新：2026-08-09；适用版本：0.6.0-dev；兼容基线：0.5.0

这里保存板级接线、虚拟板模型和硬件验收代码。平台无关引擎仍在 `src/`；port 可以包含
硬件 glue、生成资源、板级构建文件和验收 fixture。

| 目标 | 从这里开始 | 范围 |
| --- | --- | --- |
| 理解最小板级接入 | `embedded_host_demo/` | Host 侧参考，不是真实硬件 |
| 没有开发板时估算趋势 | `virtual_board/` | 桌面模型，不是 MCU 时序证据 |
| 构建、烧录和验收 ESP32-S3 | `esp32s3-idf/` | ESP-IDF、板驱动和移植侧报告 |
| 新增其他板族 | 新建同级 port 目录 | SDK、panel、input 留在 port 内 |

新 port 先读 `../docs/porting_work_guide_zh.md` 和最接近的 port README。报告必须分开记录
核心渲染、格式转换、present/DMA、内存、输入和视觉检查；桌面估算不能替代实机证据。
