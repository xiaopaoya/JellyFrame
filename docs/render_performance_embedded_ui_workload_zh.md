# Render Core 嵌入式 UI 对照 workload V0

> 状态：Stage 3 fixture qualification 已通过，正式对照矩阵待执行；最后更新：2026-09-18；适用版本：0.6.0-dev
> 适用范围：ESP32-S3 retained UI port 与等价桌面 capture

本文定义一个比单一 fill/gradient 更接近真实 App 的固定 workload，用于关闭
Render Core 性能主线的 Stage 3 对照缺口。它不要求设备 framebuffer readback、
照片或逐像素 hash；设备正确性使用固定状态检查和原始串口日志记录。

## 1. 固定页面

页面必须在 172x320、RGB565、黑色或近黑色背景上包含以下稳定结构：

- 顶部标题和一行短状态文本；
- 一个状态卡，包含圆角背景和一段短文本；
- 四个设置行，每行包含标签、可点击开关或受限表单控件；
- 至少一个分隔线或留白节点；
- 底部三项导航，包含当前选中状态；
- 一段会发生等长度替换的文本，用于局部 text/layout 更新。

页面不得在 workload 期间改变 DOM 节点数量、资源集合、字体集合或 viewport。文本、
控件 ID、颜色、初始开关状态和初始滚动位置必须写入 `metadata.json`，baseline 与
candidate 完全一致。

## 2. Workload 矩阵

每个 workload 对 baseline 和 candidate 各执行至少 3 次有效重复，建议 5 次。性能
优化对照两侧使用相同的 profile 配置；profile OFF/ON 只用于单独测量诊断开销，不能
把 profile ON candidate 与 profile OFF baseline 混成优化结论。

| ID | 固定动作 | 主要观察指标 |
| --- | --- | --- |
| `embedded-ui-static` | 等待稳定后按固定节奏切换一个状态卡的显示状态 | frame、paint、present、dirty 面积 |
| `embedded-ui-local-update` | 依次切换两个设置行并替换等长度状态文本 | script/update、style、layout、text paint、dirty |
| `embedded-ui-scroll` | 从固定起点连续拖动设置面板到固定终点，再返回 | input、frame、paint、present、DMA、长尾 |
| `embedded-ui-full-repaint` | 执行一次明确的全屏状态切换 | full-frame、convert、present、DMA、内存 |

每次重复都必须使用相同冷启动、30 个 warm-up active present 和 120 个 measured
active present。无法完成窗口时保留原始日志并标记 `PARTIAL`，不得填充缺失帧。

## 3. 设备执行要求

执行前固定并记录：

- 板卡序列号、panel controller、viewport、RGB565、总线类型/频率和供电条件；
- baseline/candidate commit、Core/Runtime/ABI、SDK lock、ESP-IDF、编译器和固件 SHA-256；
- 完整 sdkconfig、App 资源/字体摘要、页面初始状态和输入脚本；
- 温度、触摸连接状态、串口端口、操作者和每次重复时间。

测量窗口内只能有一个串口采集进程。不得运行 VS Code 设备轮询、JFDP provider、截图
程序或其他 monitor；不得改变 CPU/SPI/DMA、任务优先级、日志级别或 panel 初始化参数。

每个重复至少保存：

```text
repeat-01/
  firmware.sha256
  sdkconfig
  boot.log
  console.log
  input.txt
  visual-check.md
  result.json
```

使用 [Render Core 性能硬件对照测试要求](render_performance_hardware_comparison_zh.md)
生成两侧 manifest 和 comparison。`visual-check.md` 使用固定状态检查，不要求附图：

```text
Visual check: PASS | FAIL | PARTIAL
OFF and ON reset state: identical | not-identical
Initial: title/status/card/rows/navigation layout and clipping
Local update: toggle state, text wrapping, stale content and residual pixels
Scroll active: input follows, dirty update, bottom edge and wrong-line artifacts
Final: expected content, selected navigation and teardown state
Console: no panic/watchdog/reset/brownout/DMA/SPI/panel/present/touch errors
Conclusion: visually-equivalent-only | not-comparable
Observer / time:
```

照片、录像和 framebuffer hash可以作为附加证据；缺少它们不影响合规性。

### 3.1 当前 fixture qualification

2026-09-18 已在 WS147 上完成四个独立 clean profile 的资格验证，归档位于
`ports/esp32s3-idf/test_artifacts/embedded-ui-fixture-qualification/`。测试固件来源为
`a15aad75`；随后提交 `7807874f` 只修改 VS Code 主机扩展、CI、测试和文档，未修改
ESP32-S3 固件、Render Core、Runtime、fixture 或设备 profile 协议。因此结果仍应归因于
`a15aad75`，但在本项资格验证范围内与 `7807874f` 等价，无需重跑或改写固件版本。

四组 clean capture 均为 `status=pass`，各包含一个完整窗口、30 个 warm-up present、
120 个 measured present、五类 profile record，且 `partial=0`、`contaminated=0`、
`present_failures=0`；固定状态人工检查均通过。记录仍为 `timing_complete=0`、
`missing=script_us`，所以本次结果只关闭 fixture 和采集链路资格验证，不提供完整脚本阶段
计时，也不构成 baseline/candidate 性能结论。初次 `static/` 污染采集仅作为诊断证据保留，
不属于通过结果。

正式 baseline/candidate 矩阵前，先用当前主线分别构建四个独立目录，确认 fixture 本身
能够完成窗口。要求 ESP-IDF 5.3 或更高版本；不要复用 build 目录：

```powershell
cd ports\esp32s3-idf
$profiles = @("static", "local_update", "scroll", "full_repaint")
foreach ($profile in $profiles) {
  idf.py -B "build-ws147-embedded-ui-$profile" `
    -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_embedded_ui_$profile.defaults" build
}
```

依次刷写每个 build，并在唯一串口采集进程中取得一个完整窗口：

```powershell
idf.py -B build-ws147-embedded-ui-static -p COMx flash
python tools\collect_device_profile_window.py `
  --port COMx `
  --output test_artifacts\embedded-ui-fixture-qualification\static `
  --timeout 90 `
  --reset
```

对 `local_update`、`scroll` 和 `full_repaint` 重复上述命令并使用对应 build/output。四组
`capture.json` 都必须为 `status=pass`，五类 profile record 的 window 相同。固定状态检查：

- `static`：状态滑块在 25/75 间切换，其他文本与布局稳定；
- `local_update`：Focus mode、Quiet hours 和 PAUSE/READY 按固定节奏切换，无残影或错误换行；
- `scroll`：设置列表往返滚动，拖动中和停止后均无底部错行、旧行残留或跳位；
- `full_repaint`：`#screen` 背景和状态卡边框在 base/alt 间切换，整屏无未更新区域。

该 qualification 只验证 fixture 可用性和采集完整性，不构成性能优化结论。通过后再选择
明确的 baseline/candidate commit，按本文其余要求执行每侧至少三次的正式矩阵。

## 4. 通过标准

每组每个 workload 必须满足：

- 五段 profile record 完整、窗口号一致、`partial=0`、`contaminated=0`；
- `frames=120`、`present_frames=120`、`present_failures=0`；
- 无 panic、watchdog、brownout、reset、DMA/SPI/panel/touch 错误；
- 固定状态检查通过，且没有新增布局、裁剪、残留、错行或控件状态回归；
- `internal_free_min`、PSRAM 和 stack low-water 不越过 port 安全下限；
- 目标指标 p95 至少改善 5%，且 frame p95 不回归超过 3%；结构性优化收益不足 5% 时，
  必须同时提供 microbench/trace 解释，并证明 frame/paint/present p95 不回归超过 3%；
- candidate 内存 low-water 相对 baseline 不下降超过 5%，除非有明确解释并获 port 负责人接受。

没有可靠回读时，manifest 使用：

```json
"visualEvidence": {
  "status": "visual-equivalent-only",
  "method": "operator-checklist",
  "record": "visual-check.md"
}
```

这一路径在比较器中可以得到 `PASS`。`missing` 表示没有任何固定状态检查，结果只能为
`PARTIAL`；`fail` 直接为 `FAIL`。

## 5. 结果边界

本 workload 可以用于比较常见嵌入式 UI 组合的 Core/Runtime 阶段成本，不能单独推出：

- JellyFrame 对所有 App、所有设备或所有图形库的总体性能排名；
- 某个 DOM 元素在 MCU 上的精确耗时；
- 触摸 input-to-present 延迟，除非 port 另有可靠的输入时间戳和 present 时间戳；
- 没有显示回读时的逐像素等价结论；
- Core paint 改善必然带来设备端到端帧率改善。

Stage 3 的退出条件是：本 workload 四个场景完成至少 3 次重复，生成可复核的
comparison JSON/HTML，固定状态检查和稳定性通过，并将其与既有 `static-local-repaint`、
`text-layout-update`、`drag-scroll`、`full-repaint` 结果分开报告。
