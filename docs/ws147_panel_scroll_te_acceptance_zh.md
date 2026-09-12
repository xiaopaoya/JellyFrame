# WS147 Panel Scroll 同步修复与移植验收

> 状态：待硬件确认与实机实现；适用板卡：Waveshare ESP32-S3-Touch-LCD-1.47；面板：JD9853；分辨率：172x320

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

GRAM probe 另提供一个仅用于诊断的 `VSCSAD-before-strip` 构建变体，用来判断命令顺序是否改变现象；该变体不是修复，也不得用于生产路径。

## 3. 实现前必须确认

1. 核对 WS147 实际 JD9853 模组是否引出 TE 信号，确认 GPIO、有效边沿、电平和是否与触控/背光复用。
2. 确认 JD9853 的 TE 配置命令、TE 模式和 `VSCSAD` 更新时序；以实际模组资料和逻辑分析仪结果为准，不凭 ST7789 资料类推。
3. 确认 TE 脉冲代表帧开始、帧结束还是可写窗口；记录面板扫描周期和可用 blanking 时间。
4. 若模组未引出可用 TE，必须选择并记录替代方案：保持 full-frame/fallback，或在 `DISPOFF` 保护下更新。没有同步证据时不得采用“延时若干微秒”的伪修复。

## 4. 推荐实现顺序

1. 增加端口配置：TE GPIO、有效边沿、等待超时和测试开关，默认关闭。
2. 增加受控 TE 等待与超时统计；超时或信号异常必须返回失败并走完整 framebuffer present。
3. 在同一 LCD 锁内组织“等待安全窗口、更新 strip、切换 VSCSAD”的事务，避免其他 LCD 命令插入。
4. 保留已有的环形映射与跨尾部分段逻辑，只改变同步时序；不得用坐标偏移掩盖显示扫描竞态。
5. 扩展 telemetry，至少记录 `te_wait_us`、`te_timeouts`、`vscsad_updates`、`strip_dma_done`、`panel_scroll_fallbacks` 和滚动方向。
6. 为不支持 TE、TE 超时、DMA 失败、非连续 dirty 区和普通 present 重新进入路径保留明确的 fallback/re-entry 行为。

## 5. 实机测试矩阵

每个 case 使用同一最终固件、同一设备和固定机位视频；A/B 只改变 sink/scroll 配置，不改变 App、输入轨迹或构建参数。

| Case | 操作 | 必须观察 |
| --- | --- | --- |
| 初始静态 | 启动、首帧、停止滚动 | 无错位、无旧帧残留，TE 初始化状态正确 |
| 小幅上滑 | 1、2、3、5、8 行连续上滑 | 下边缘无旧行，事件顺序与画面顺序一致 |
| 小幅下滑 | 同上 | 上边缘无旧行，方向映射正确 |
| wrap 前后 | 连续滚动跨越 GRAM 尾部至少 3 次 | 无断行、重复行或跳行，wrap 计数与轨迹一致 |
| 快速连续滚动 | 连续输入 120 个 move 样本 | 无撕裂；无输入丢失或需记录丢失原因 |
| TE 异常 | 屏蔽/断开 TE 或触发超时 fixture | bounded timeout，自动 full present，之后可重新进入 |
| DMA 异常 | 注入已有端口 DMA 失败点 | 不发布半帧；状态复位，后续 full present 成功 |
| 普通重绘交错 | 滚动中插入按钮状态、文本和非滚动区域更新 | 正确退出/恢复 panel-scroll，不显示 stale rows |
| 长时间 | 至少 30 分钟，包含正反向滚动和至少 100 次 wrap | 无行错位、panic、watchdog、reset、DMA/SPI/panel/present error |

## 6. 通过标准

- 人工直接观察和固定视频均无下边缘/上边缘行错位、重复行、跳行、撕裂或错误内容。
- 每个输入样本可关联到 dispatch、mutation/frame、paint-end、DMA-complete；只能在增加 TE/latch 或光学测量后声明 input-to-present，否则统一称 input-to-DMA-complete。
- TE 等待、VSCSAD 更新、strip DMA 完成和 fallback 次数事件守恒；不得静默丢失。
- 正常、wrap、超时、DMA 失败和普通重绘交错路径均可恢复；fallback 后至少一次完整 present 成功，再允许 re-entry。
- 设备日志无 panic、watchdog、brownout、reset、DMA、SPI、panel 或 present 错误；内存低水位不劣于 framebuffer 基线，除非报告解释原因。
- 只有上述证据齐全后，才可在独立 commit 中打开 `JELLYFRAME_WS147_PANEL_SCROLL_VISUAL_ACCEPTED`，并重新生成版本化报告和固件 hash。

## 7. 当前证据与禁止结论

- `ws147-panel-scroll-ab-9e180ec2-20260911-1520`：性能与 fallback telemetry 通过，但 visual result 为 `fail-lower-edge-row-displacement`。
- 当前主线额外拒绝 `scroll_gesture.dragging()` 和 `scroll_gesture.has_inertia()` 状态的 panel-scroll 候选；该保护必须随当前主线重新刷入设备后才算实机生效。
- 现有自动滚动 profile 没有真实触控输入、事件 correlation 或面板可见时刻，不能证明拖动跟手性或 input-to-present。
- 在本任务关闭前，不得将 B 路径加入 Developer Image 默认能力、作者工具能力矩阵或对外性能宣传。
