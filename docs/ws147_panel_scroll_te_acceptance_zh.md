# WS147 Panel Scroll 同步修复与移植验收

> 最后更新：2026-11-05；适用版本：0.6.0-dev
> 状态：公开的 WS147 原理图未提供板级 TE 输入；实验路径继续保持关闭，等待经验证的非 TE 同步设计和实机验收。

## 1. 目的与边界

本任务只处理 WS147 的实验性 physical-GRAM panel-scroll 路径。它不修改 Render Core 的滚动算法，也不把桌面自动滚动性能数字当作真实触控延迟证据。

当前路径的性能收益已被自动滚动 A/B 证明，但最新实机复测存在人工可见的下边缘行错位，因此在本任务完成前不得启用 `JELLYFRAME_WS147_PANEL_SCROLL_VISUAL_ACCEPTED`，也不得将该路径作为默认可用能力发布。

交互策略已经明确：真实触控拖动及其惯性阶段不得使用 physical-GRAM panel-scroll。即使测试配置显式打开了实验路径，只要 `scroll_gesture.dragging()` 或 `scroll_gesture.has_inertia()` 为真，候选帧也必须退出 panel-scroll，并在同一帧重建 framebuffer 后走普通 framebuffer scroll-blit/full present。实验 panel-scroll 仅保留给无人触控的自动 benchmark，不能作为拖动跟手性的实现或证据。

## 2. 已定位问题

当前实现的环形地址映射满足：

- `delta_y > 0` 时，暴露区为逻辑底部，写入旧 `VSCSAD` 对应的物理起点；
- `delta_y < 0` 时，暴露区为逻辑顶部，写入新 `VSCSAD` 对应的物理起点；
- 跨 GRAM 尾部时分成两次连续 strip 写入，数据指针按第一段行数递进。

当前 [waveshare_touch_lcd_boards.cpp](../ports/esp32s3-idf/main/boards/waveshare_touch_lcd_boards.cpp) 的实际顺序是先执行 strip 的 `RAMWR`/DMA，再发布 `VSCSAD`；初始化还发送了 `TEOFF`。此前文档中的顺序描述已过时。即使 DMA 已完成，面板扫描与地址切换仍没有共同的 TE/vblank 边界，面板可能在地址切换附近取到旧 GRAM 行，表现为边缘行错位。

2026-11-05 检查了 [Waveshare WS147 官方原理图](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.47/ESP32-S3-Touch-LCD-1.47-Schematic.pdf)：LCD 接口 J3 列出 17 个引脚，没有 TE 信号连接到 ESP32-S3。用于检查的归档 PDF SHA-256 为 `DB1F05C28BBD078E76B430FF2A1332FE7E566731104756A73C84F08C38333AE5`。这证明公开板级连接没有提供 TE GPIO；不代表 JD9853 芯片本身或其他模组修订版不支持 TE。不得猜测 GPIO，也不得根据其他面板的行为启用此路径。

GRAM probe 另提供一个仅用于诊断的 `VSCSAD-before-strip` 构建变体，用来判断命令顺序是否改变现象；该变体不是修复，也不得用于生产路径。

## 3. 实现前必须确认

1. 对公开原理图对应的 WS147 按 TE 不可用处理，除非特定硬件修订版的原理图或直接电气检查能证明另有连接。
2. 安全默认方案是 full-frame/fallback。`DISPOFF` 事务只有在确认 JD9853 命令支持、测量可见黑屏时长后才可考虑；交互滚动可能无法接受其可见闪黑。
3. 不得新增该板的 TE GPIO 配置。若不同修订版暴露 TE，记录引脚、极性、边沿及复用，并用模组资料和逻辑分析仪确认 JD9853 TE 模式、脉冲含义和 blanking 时间。
4. 没有同步证据时不得采用“延时若干微秒”的伪修复。

## 4. 推荐实现顺序

1. physical-GRAM panel-scroll 继续保持默认关闭；公开 WS147 板级连接不增加 TE GPIO 配置。
2. 任何未来同步实现都必须对等待或事务失败设置有界处理，并走完整 framebuffer present。
3. 只有选定的同步机制经过验证后，才在同一 LCD 锁内组织“同步、更新 strip、切换 VSCSAD”的事务，避免其他 LCD 命令插入。
4. 保留已有的环形映射与跨尾部分段逻辑，只改变同步时序；不得用坐标偏移掩盖显示扫描竞态。
5. 扩展 telemetry，至少记录同步模式/失败、`vscsad_updates`、`strip_dma_done`、`panel_scroll_fallbacks` 和滚动方向。
6. 为同步不可用、同步事务失败、DMA 失败、非连续 dirty 区和普通 present 重新进入路径保留明确的 fallback/re-entry 行为。

## 5. 实机测试矩阵

每个 case 使用同一最终固件、同一设备和固定机位视频；A/B 只改变 sink/scroll 配置，不改变 App、输入轨迹或构建参数。

| Case | 操作 | 必须观察 |
| --- | --- | --- |
| 初始静态 | 启动、首帧、停止滚动 | 无错位、无旧帧残留，同步模式记录正确 |
| 小幅上滑 | 1、2、3、5、8 行连续上滑 | 下边缘无旧行，事件顺序与画面顺序一致 |
| 小幅下滑 | 同上 | 上边缘无旧行，方向映射正确 |
| wrap 前后 | 连续滚动跨越 GRAM 尾部至少 3 次 | 无断行、重复行或跳行，wrap 计数与轨迹一致 |
| 快速连续滚动 | 连续输入 120 个 move 样本 | 无撕裂；无输入丢失或需记录丢失原因 |
| 同步不可用 | 无 TE、同步超时或事务失败 fixture | 有界失败，自动 full present；恢复后才允许重新进入 |
| DMA 异常 | 注入已有端口 DMA 失败点 | 不发布半帧；状态复位，后续 full present 成功 |
| 普通重绘交错 | 滚动中插入按钮状态、文本和非滚动区域更新 | 正确退出/恢复 panel-scroll，不显示 stale rows |
| 长时间 | 至少 30 分钟，包含正反向滚动和至少 100 次 wrap | 无行错位、panic、watchdog、reset、DMA/SPI/panel/present error |

## 6. 通过标准

- 人工直接观察和固定视频均无下边缘/上边缘行错位、重复行、跳行、撕裂或错误内容。
- 每个输入样本可关联到 dispatch、mutation/frame、paint-end、DMA-complete；没有 TE/latch 或光学测量时统一称 input-to-DMA-complete。
- 同步等待/失败、VSCSAD 更新、strip DMA 完成和 fallback 次数事件守恒；不得静默丢失。
- 正常、wrap、同步失败、DMA 失败和普通重绘交错路径均可恢复；fallback 后至少一次完整 present 成功，再允许 re-entry。
- 设备日志无 panic、watchdog、brownout、reset、DMA、SPI、panel 或 present 错误；内存低水位不劣于 framebuffer 基线，除非报告解释原因。
- 只有上述证据齐全后，才可在独立 commit 中打开 `JELLYFRAME_WS147_PANEL_SCROLL_VISUAL_ACCEPTED`，并重新生成版本化报告和固件 hash。

## 7. 当前证据与禁止结论

- `ws147-panel-scroll-ab-9e180ec2-20260911-1520`：性能与 fallback telemetry 通过，但 visual result 为 `fail-lower-edge-row-displacement`。
- 当前主线额外拒绝 `scroll_gesture.dragging()` 和 `scroll_gesture.has_inertia()` 状态的 panel-scroll 候选；该保护必须随当前主线重新刷入设备后才算实机生效。
- 现有自动滚动 profile 没有真实触控输入、事件 correlation 或面板可见时刻，不能证明拖动跟手性或 input-to-present。
- 官方 WS147 原理图未将 LCD TE 引到 ESP32-S3；该型号当前没有可供固件等待的板级 TE GPIO。除非新的板级证据推翻此结论，否则不能按 TE/vblank 路线实现本任务。
- 在本任务关闭前，不得将 B 路径加入 Developer Image 默认能力、作者工具能力矩阵或对外性能宣传。
