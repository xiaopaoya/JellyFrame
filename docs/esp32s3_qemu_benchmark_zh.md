# ESP32-S3 QEMU PSRAM 基准记录

日期：2026-06-16

这份文档记录 JellyFrame ESP32-S3 bring-up 的 QEMU PSRAM 梯度测试。它用于确认 firmware 能启动、PSRAM 容量能承载当前 300x300 软件渲染管线，以及各阶段耗时是否有异常趋势。它不是真实 ESP32-S3 芯片的最终 FPS 或延迟数据。

## 测试环境

- ESP-IDF：`v5.3.1`
- QEMU：`esp_develop_9.2.2_20260417`
- Target：`esp32s3`
- Viewport：`300x300`
- Synthetic cards：`40`
- Iterations：`20`
- PSRAM 配置：
  - `CONFIG_SPIRAM=y`
  - `CONFIG_SPIRAM_MODE_OCT=y`
  - `CONFIG_SPIRAM_TYPE_AUTO=y`
  - `CONFIG_SPIRAM_SPEED_40M=y`
  - `CONFIG_SPIRAM_USE_MALLOC=y`

QEMU Octal PSRAM 参数：

```powershell
-global driver=ssi_psram,property=is_octal,value=true
```

原始 CSV 保留在：

- `ports/esp32s3-idf/qemu_psram_gradient_octal_results.csv`
- `ports/esp32s3-idf/qemu_psram_gradient_quad_results.csv`

## Octal PSRAM 结果

所有耗时均为每轮平均微秒。

| QEMU PSRAM | 检测到的 PSRAM | 测试前空闲 PSRAM | HTML Parse | CSS Parse | Render Tree | Layout | Layer Tree | Flatten | Render Frame | Present RGB565 | Full Pipeline |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 4M | 4 MB | 4,191,548 | 32,286.25 | 3,523.90 | 56,566.20 | 15,747.70 | 9,554.15 | 414.05 | 24,328.10 | 34,794.05 | 168,014.35 |
| 8M | 8 MB | 8,385,720 | 32,277.15 | 3,497.65 | 56,222.75 | 16,207.85 | 9,560.95 | 417.80 | 24,206.70 | 34,742.05 | 168,882.25 |
| 16M | 16 MB | 16,774,196 | 32,335.10 | 3,552.80 | 56,132.40 | 15,913.90 | 9,518.40 | 410.40 | 24,615.70 | 34,359.75 | 167,822.50 |
| 32M | 32 MB | 32,895,920 | 32,637.85 | 3,546.60 | 55,988.65 | 15,645.10 | 9,497.15 | 425.55 | 24,545.10 | 34,488.30 | 168,361.95 |

## Quad 模式对照

使用 `CONFIG_SPIRAM_MODE_QUAD=y` 和 `CONFIG_SPIRAM_TYPE_AUTO=y` 时：

| QEMU PSRAM | 结果 |
|---:|---|
| 4M | 启动，检测到 4 MB PSRAM，完整基准完成。 |
| 8M | 启动，检测到 8 MB PSRAM，完整基准完成。 |
| 16M | 在 ESP-IDF v5.3.1 该配置下 PSRAM 物理尺寸检测失败。 |
| 32M | 在 ESP-IDF v5.3.1 该配置下 PSRAM 物理尺寸检测失败。 |

该失败在 Octal PSRAM 仿真下消失，所以完整 4M-32M 数据采用 Octal 模式。

## 结论

PSRAM 容量从 4 MB 增加到 32 MB，并不会显著改变本基准的 CPU 阶段耗时。parser、render tree、layout、framebuffer render、RGB565 present 和 full pipeline 都处于正常波动范围内。对这个 workload 而言，容量主要影响内存余量，而不是直接提升渲染速度。

当前 300x300 基准需要同时容纳 RGBA framebuffer、RGB565 presentation buffer、DOM/style/layout/layer 分配，以及 full pipeline 临时对象。4 MB PSRAM 可以跑通，但属于最低可行层级。

实际选型建议：

- 4 MB：可用于 bring-up、简单表盘和受限 demo，余量很少。
- 8 MB：推荐作为 300x300 小 UI 和适度资源的起步基线。
- 16 MB：如果要启用 JerryScript、中文字库、多页面、图片或资源缓存，优先选择。
- 32 MB：当前基准不直接受益，只有在应用确实需要大资源缓存或图像型 UI 时再选择。

下一步必须在真实开发板上复测屏幕 flush、触摸输入、字体包和 JerryScript，因为 QEMU 无法替代真实 SPI/8080 总线、PSRAM 带宽和调度噪声。

