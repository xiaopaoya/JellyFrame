# Script Task Value Frame v4 移植接入与验收要求

最后更新：2026-08-27；适用版本：0.6.0-dev  
主线基线：`f8b1c3a`（或其后仅包含修复的提交）  
适用范围：ESP32-S3 / WS147 的 Script Task worker-to-UI value-frame 路径  
状态：移植接入要求；v4 在完成本文件验收前仍是实验性 profile

## 1. 目的与非目标

本轮将跨任务帧格式从 v2/v3 接入到 **v4**，使 worker 私有 DOM 中产生的
`transform: rotate()/scale()`、变换层自身 `overflow: hidden`、圆角，以及该层
下方的嵌套裁剪，在 UI task 的纯值重放中保持与 `SoftwareCompositor` 一致的语义。

v4 的新增字段是每条变换 command 的 `source_clip_index`。它指向**变换前源
surface**的 clip chain；现有 `display_clip_indices` 仍是**变换后目标空间**的
clip chain。两者不可互换，也不得由 port 自行合并为一个矩形。

本轮不做以下事情：

- 不把 v4 设为 Developer Image 或产品默认 frame profile；
- 不修改 JFDP、安装器、DMA、RGB565 转换、panel window 或 worker/task 所有权；
- 不以 v4 接入证明 retained replay、framebuffer reuse、tile renderer、30 FPS 或
  完整 CSS transform 支持；
- 不传递 `Node*`、`LayerNode*`、`jerry_value_t`、worker arena 地址、framebuffer
  指针或 port 私有对象跨 task。

## 2. 主线已提供的契约

`ScriptTaskAppFrameCodecOptions::version` 支持 1--4：

| 版本 | 可表达能力 | 遇到更高版本帧时的要求 |
| --- | --- | --- |
| v1 | 传统 display list | 拒绝 clip/transform feature |
| v2 | 有界 destination clip table | 拒绝 transform feature |
| v3 | 1/1024 定点 affine transform | 拒绝 v4 source clip feature |
| v4 | v3 + source clip chain | 本轮唯一允许接收的目标版本 |

v4 编码/解码在以下情况必须确定性拒绝：版本不匹配、source/destination clip
index 越界、parent index 非递增或成环、clip depth 超限、保留字节非零、frame
payload 超限、无效 transform 或无效 display command。拒绝后 UI task 不得 present
半帧，且必须释放 sealed frame lease；后续正常帧仍可被接收。

`ScriptTaskFrameRenderer::render_into()` 已在 UI 侧实现：

1. destination clip chain 在 affine sampling 后应用；
2. source clip chain 在有界 `transformed_surface` 内 rasterize；
3. 变换层自身的 clip 保留 compositor 所需的 destination 约束；
4. 已处于祖先 affine transform 下的子层 clip 只在 source surface 生效，不能在
   affine mapping 后重复裁剪；
5. source clip chain 与普通 destination chain 都受 `max_clip_depth` 限制。

worker 继续只调用 `ScriptTaskWorkerRuntime::publish_frame()`；UI task 只通过
`take_script_task_app_frame()` 取得复制后的 value frame 并调用 renderer。port 不得
绕过 codec 读取 worker 的 `LayerTree`。

## 3. 移植实现要求

### 3.1 独立 acceptance profile

以现有 `jellyframe_esp32s3_script_value_frame_v2_acceptance.cpp` 为参考创建独立
v4 fixture/run mode、SDKCONFIG 和 clean build 目录；不得修改 v2 已归档结果，也
不得复用 recovery/P3 的 build 目录或 sdkconfig。

建议命名：

```text
run mode: script-task-value-frame-v4-acceptance
sdkconfig: sdkconfig.ws147_script_task_value_frame_v4.defaults
build:     build-ws147-script-task-value-frame-v4
artifact:  test_artifacts/script_task_value_frame_v4_ws147_<commit>_20260827/
```

`ScriptTaskAppFrameCodecOptions` 必须使用具名字段赋值，不能使用仅含前四个成员的
旧聚合初始化。最小要求如下，实际预算可增大但不能静默减小：

```cpp
ScriptTaskAppFrameCodecOptions options;
options.version = 4;
options.max_commands = <declared worker command budget>;
options.max_text_bytes = <declared text budget>;
options.max_input_targets = <declared input-target budget>;
options.max_payload_bytes = <sealed frame payload budget>;
options.max_clips = <declared clip budget>;
options.max_clip_depth = <declared clip-depth budget>;
```

同一个 `frame_options()` 结果必须传给 worker runtime 与 UI task
`take_script_task_app_frame()`。两端版本或任何 budget 不一致都属于配置错误，
不能用“无 frame”或普通 v1 页面掩盖。

### 3.2 UI 接收与呈现

每个 received frame 必须依次执行：

1. 用 active `ScriptAppSession` 调用 `take_script_task_app_frame()`；
2. 仅接受 `Accepted`，并记录 packet/frame sequence；
3. 严格比较 `frame.viewport` 与实际 framebuffer 尺寸；不一致时计为
   `render_rejected`，不得调用 present；
4. 用 `ScriptTaskFrameRenderer::render_into()` 和可复用的
   `SoftwareRasterizerScratch` 完成 raster；
5. 仅当 renderer 返回 `true` 后调用现有 sink/panel present，并在成功后推进
   `ui_accepted_frame_seq`；
6. decode/render/present 任一失败时不提交部分输出，保留原因计数，并允许下一帧正常
   恢复。

`ScriptTaskFrameRendererOptions::max_temporary_pixels` 必须根据实际 RAM budget
显式设置。它约束的是 transformed source surface，不能只沿用 v2 rounded clip
temporary-surface 的统计。每帧复用 scratch，禁止为每一个 command 重新分配
framebuffer 或把无界临时 surface 放入 worker task。

本轮第一轮正确性验收可完整重绘。若同时测 dirty，dirty rect 必须是保守的
destination-space 区域；不能把 source clip、transform bounds 或旧 v2 dirty 规则
当作不足覆盖的 dirty rect。

## 4. Fixture 与必测流程

### A. 基线与 wire agreement

1. clean configure、build、flash、reset；记录 commit、ESP-IDF revision、board、屏幕
   尺寸、PSRAM、SDKCONFIG、ELF/map hash。
2. 启动 v4 fixture，日志必须同时确认 worker 与 UI 的 `codec_version=4`、全部
   codec budget、frame lease budget、`max_temporary_pixels`、实际 viewport。
3. 发布至少 20 个正常静态 frame；要求 decode/render/present 全部 accepted，且
   published/taken/ui accepted sequence 单调。
4. 在相同固件上执行既有最小 v1/P3 worker smoke；v4 acceptance 不得改变其默认
   codec 或运行行为。

### B. 单变换层 source clip

创建固定页面：一个具有 `overflow: hidden` 和非零 `border-radius` 的容器，容器内
有高对比 fill/gradient/text，容器做 `rotate(90deg)` 或固定小角度旋转。内容必须故意
超出容器边界。

观察：

- 变换后的容器外没有填充、渐变、文本或圆角外漏色；
- 可见区域不被错误裁成未变换的矩形；
- 变换期间无旧位置残影、重影或透明边缘的异常黑边；
- telemetry 证明至少一条 accepted command 的 transform enabled，且至少一条
  `source_clip_index != kScriptTaskNoClip`。

### C. 祖先变换下的嵌套 source clip

在 B 的变换容器内加入子容器。子容器也设置 `overflow: hidden`，其矩形 clip 与
父 clip 仅部分相交；子内容应明显越过二者边界。至少一组使用直角旋转，至少一组
使用非 90 度角；圆角可作为额外场景，不替代矩形嵌套场景。

观察：

- 父、子边界都约束内容；
- 子 clip 不会在 destination-space 再切一遍，因而不会产生未变换矩形缺口；
- 没有 clip chain 漏用导致的越界绘制；
- frame 中至少存在两层 source clip parent chain，报告最大 source clip depth。

桌面 reference 必须由相同 fixture 的 `SoftwareCompositor` 产生。可以采用已批准的
像素 hash/采样点或逐像素比较，不需要手机照片；若只做人工目检，报告必须逐项列出
父 clip、子 clip、旋转、圆角和动画的观察结论。

### D. 动画、输入与 dirty

用 worker 私有 timer 或输入 listener 改变 rotation/translation、文本或颜色，完成：

- 至少 300 次 accepted/present 更新；
- 至少一次变换层与子内容在同一更新周期内变更；
- 若启用 dirty：一次内容中央 dirty 与一次穿过变换后边缘的保守 dirty；
- 若 dirty 尚无可靠规划，可完整重绘并将 dirty 标为 `not-tested`，但不得伪造局部
  刷新结果。

目检应特别检查旋转边缘、圆角、渐变和文本：不得有重影、旧帧残留、矩形漏色、错位
或只显示首帧。

### E. 明确拒绝与恢复

从可接受的已编码 v4 packet 单字节构造以下独立用例，保持 session 正确：

1. `source_clip_index` 超出 clip table；
2. source clip parent 指向自身；
3. source clip parent 指向未来 index；
4. source clip chain 超过 configured `max_clip_depth`；
5. v4 packet 被 v3 options 接收；
6. transform source temporary surface 超过 `max_temporary_pixels`；
7. viewport 与 framebuffer 不一致。

每个用例必须记录 typed take/decode/render rejection、无 present、lease release，随后
至少显示 5 个正常 v4 frame。终态必须恰有 7 个预期拒绝，且 `unexpected_rejections=0`；
禁止通过 MCU reset、重新烧录、强杀 UI task 或回退 v1 来宣称恢复成功。

### F. 有界资源与退出

完成 C/D 后正常停止 worker、UI 和 supervisor：worker 调用自身 `stop()`，随后由
supervisor 做既定两阶段 teardown。记录 worker/UI completion、frame lease 回收、heap
low-water 和 post-teardown 状态。要求无 panic、watchdog、brownout、reset、DMA/SPI/panel
error、failed flush、double release 或 session 泄漏。

本轮不要求性能门槛，但必须单独记录 render、present、render+present 的 p50/p95；如果
v4 相比同 fixture 的 v3 或 direct compositor 路径明显恶化，只报告数据并归因到 source
raster、affine sampling、RGB565 conversion 或 panel present，不能把总耗时归咎于“v4”。

## 5. 必须采集的 telemetry

`summary.json` 至少包含：

- `git_commit`、`profile`、`codec_version`、board/viewport、SDKCONFIG/ELF/map hash；
- 全部 frame codec、lease、clip depth、temporary pixel budget；
- published/take accepted/take rejected/render rejected/present success/present failed；
- `published_frame_seq`、`ui_accepted_frame_seq`、最大 command count；
- destination clip count/depth、source-clip command count、最大 source clip depth；
- transform command count、singular-transform skipped count、最大
  `transformed_surface.pixels.size()` 与 ordinary temporary surface size；
- full/dirty frame count、render/present/render+present p50/p95；
- internal/PSRAM low-water、diagnostic count、frame lease release count；
- watchdog/reset/panic/brownout/DMA/SPI/panel/failed-flush error counters；
- 每个 E 用例的 rejection reason、present count 和后续恢复 frame count，以及总
  `expected_rejections` 与 `unexpected_rejections`。

`report.md` 还必须写明 B--D 的人工观察或桌面 reference 比较方式，以及 C 中嵌套
source clip 不是由 port 私有直接画图伪造。

## 6. 通过条件与交付物

通过要求：A--C、E、F 全部通过；D 至少完成完整重绘的动画/输入正确性。若 dirty
没有独立可靠证据，保持 `not-tested`，不影响 v4 正确性结论但不得写成已验证优化。

以下任一情况为 `partial` 或失败：worker/UI codec version 不一致、transform/source clip
未实际出现、source clip 被忽略或重复 destination clipping、任何 reject 后无法连续恢复、
任一异常重启/硬件错误、非 E 用例的 rejection、temporary budget 越界后仍 present、或使用
跨任务指针绕过 value frame。

交付目录：

```text
test_artifacts/script_task_value_frame_v4_ws147_<commit>_20260827/
  report.md
  summary.json
  raw.log
  normalized.log
  build.log
  flash.log
  sdkconfig
  elf-map-hashes.txt
  desktop-reference/        # 若使用像素/hash 比较
  malformed-recovery.log
```

该报告只决定 v4 profile 是否可作为后续 Device OS / Script Task 的候选接入；不改变
0.6 的 Canvas、Modern Paint 或总体发布结论。
