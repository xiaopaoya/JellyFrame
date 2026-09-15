# jellyframe 0.6.1 代码审查 — 汇总与交叉核对

> 最后更新：2026-09-14；适用版本：0.6.0-dev

审查范围：`render_core`（`src/render_core/`，42 个 `.cpp`）与 `app_runtime`（`src/app_runtime/`）。
审查口径：性能、实现正确性、可读性。**不含安全审查**（按用户说明，这些是善意代码）；不提出重写方案，只给最小、局部的修复。

五份分册报告：

| 分册 | 覆盖模块 | 发现数 |
| --- | --- | --- |
| `render_core_code_review.md` | render_core 首批 12 个文件（光栅、文本、脏矩形、绘制命令） | 3 严重 / 9 中等 / 6 可读性 |
| `review_findings_render_core_rest.md` | render_core 其余全部 `.cpp` | 6 High / 9 Medium / 20 Low |
| `review_findings_app_runtime_services.md` | `app_compute_jobs`、`app_video_frames`、`app_font_set`、`app_services`、`host_services`、`app_host`、`app_service_worker` | 4 High / 7 Medium / 5 Low |
| `review_findings_app_runtime_lifecycle.md` | `app_lifecycle`、`app_budget`、`app_load_telemetry`、`app_host_data`、`app_frame_policy`、`app_storage_lifecycle_policy`、`system_events`、`app_installed_bundle`、`app_capability_broker`、`authorized_file_broker`、`app_device_services` | 5 High / 6 Medium / 3 Low |
| `review_findings_app_runtime_script_task.md` | script-task 子系统 10 对 `.cpp/.h` | 4 High / 7 Medium / 6 Low（共 17 条） |

`render_core_code_review.md` 是中文撰写的首批报告，其余四份为英文；术语与分级口径一致。

---

## 跨模块反复出现的三个模式

审查最值得注意的结论不是单条发现，而是同一类错误在互不相干的文件里反复出现。按修复收益排序：

**一、热路径上的重复拷贝。** 这是本次占比最高的性能问题，且集中在最重的缓冲区上：视频帧、解码表面、网络响应体、计算结果、音频 URL、保留帧与帧缓冲，全部在完成路径上整体复制一次，而源对象随后就被销毁或在 `pending_.erase()` 中释放。涉及 `app_video_frames.cpp:203-206`、`app_services.cpp:1095-1104`、`559-566`、`501`、`1339-1346`、`app_compute_jobs.cpp:185-190`，以及 script-task 册的 #16/#17、`render_core` 的 L19（`apply_styles_iterative` 每个节点整份 `Style` 拷贝，已核对 `style_repaint.cpp:158`）。共同修法是同一套：让源对象可移动，或在记录里持 `shared_ptr`/`span`，把拷贝降到零或一次。

**二、O(n²) 的查找-删除与线性去重。** `app_device_services.cpp:322-334`、`513-530`、`532-544` 三处都是「先收集 handle，再逐个 `release_*`」，而 `release_sample`/`release_snapshot` 各自做一次 `find_if` + `erase`，形成 k 次全表扫描加 k 次尾部搬移（已核对 `records_.erase(found)` 在 `318`、`509`）。同形状的还有 `app_capability_broker.cpp:78` 的 `decision_exists` 去重、script-task 册 #1 的重复检测、`render_core` 的 M8 表单校验。注意 `app_device_services.cpp:338`、`548` 的 `collect_released_*` 已经用了正确的 `remove_if` 写法——同一文件里两种写法并存，修法直接照抄邻近的正确实现即可。

**三、每帧重算本可缓存的不变量。** 字体 fallback 上下文每次 measure/paint 全量重建（`app_font_set.cpp:279-304`，含两次 `fonts_` 全遍历）、图像缓存预算每次淘汰重算 O(n) 计数（`app_services.cpp:1549-1552` 是 `1683` 的 `while` 条件，已核对）、脚本任务帧渲染器每帧重建诊断串、`render_core` 中文本为计行数换行一次、绘制时再换行一次（H4）。这些数据在帧与帧之间不变，只在 mutation 时失效。

---

## 交叉核对结果

逐条回读源码验证了发现中可证伪的部分，结果如下：

**已更正。** `review_findings_app_runtime_lifecycle.md` 的发现 1 原写作「14 个调用点、静默丢弃 terminate 级条目」。回读 `app_budget.cpp` 后修正为：实际 13 个调用点（9 个 TerminateApp，4 个 Warn），且 `append_recovery_diagnostic` 开头有 `if (!meter.exhausted()) return;`（`15-17`），槽位只被真正超限的表消耗。因此 8 槽上限是**最坏情况**下的丢失，而非无条件丢失——现实中通常只有少数表触发。`kMaxDiagnostics = 8` 与调用点数不一致这一核心结论仍然成立，但严重程度应按「边界情形 + 静默回归风险」理解，而非「每次拆机都丢三条」。

**已验证成立。** `app_device_services.cpp` 的两次 O(n²) 释放循环（行号 `322-334`、`513-544` 与 `305-320`、`494-511` 完全对上）；`valid_location` 在 `app_device_services.cpp:45` 与 `app_host_data.cpp:29` 逐字节重复的坐标范围判定；`render_core` M1 的 `is_out_of_flow_positioned` 在 `layout.cpp` 中 13 处调用，均在每 box 的热路径上；`AppImageSurfaceCache::over_budget` 确为 `while` 循环条件。

**未逐条复核。** Low 级可读性条目以及 script-task 册的个别编号，未再回源码确认行号；若其中某条要与上游对账，建议先重读对应文件。

---

## 2026-09-14 处置状态

以下状态以当前 `master`（`2037f74e`）源码和本地测试为准。审查报告本身保留为历史发现记录，不把历史严重级别直接当作当前未修复数。

### 已有代码修复，等待 CI/实机证据闭环

| 发现族 | 当前依据 | 处置 |
| --- | --- | --- |
| 圆角渐变在内部区域重复做 coverage | `461a96b8` | 已实现圆角行分解；需保留截图/基准对比，确认视觉等价。 |
| 文本换行只计行数却物化完整行、重复布局结果 | `6fb699b1`、`99bcecf4` | 已提供计数路径并复用布局缓存；需 CI 与长文本基准确认。 |
| viewport 单位固定使用默认尺寸 | `9dcf1f1a` | 已按 layout context 解析；需矩形、竖屏、圆屏回归。 |
| dirty rect 无上限/合并工作量过大 | `13264bcd`、`c1111274` | 已加入边界和受控合并；需检查 profile 中的 full-frame fallback 归因。 |
| 圆角 clip、透明/变换合成和采样的冗余热路径 | `f43c06fe`、`c5d1f17d`、`cccf1928` | 已加入快路径；需设备 Profile OFF/ON 数据确认收益而非只确认正确性。 |
| flex paint 与 layout 的子项排序不一致 | `c255f0a8` | 已共享排序实现；需保留绝对定位与非零 order 回归。 |
| trace 查看器逐帧同步文件检查 | `6eae4c38` | 已改为一次性捕获/索引；需 Windows 扩展测试确认。 |
| app runtime 完成身份、句柄所有权、批量释放、缓存计数 | `e9775ef4` | 已修复并有本地测试；需 Linux sanitizer/Windows scripting CI 闭环。 |
| image cache completion URL 线性查找 | `ed3f4e64` | 已使用 job URL 索引替代逐条扫描，并在条目淘汰/释放时同步维护；App Runtime 测试通过，需 CI 闭环。 |
| script-task 脏区、clip 引用、service bridge 输入边界 | `e9775ef4`、`c557e4c0` | 已修复并有回归测试；设备端 malformed/relaunch 证据仍按验收文档执行。 |
| 字体上下文状态与字体缓存失效 | `d3cbbf56` | 已纳入 runtime 状态；需长文本/多字体实机观测。 |
| dirty-region 子树边界重复扫描 | `75a550a6` | 已改为单次迭代后序遍历，并保留嵌套脏节点回归测试；当前 Release 全套 50 项 CTest 通过，需随主线 CI 闭环。 |
| style-repaint 布局字段覆盖 | 当前 `style_repaint.cpp` | 已逐字段核对，`layout.cpp` 使用的布局字段均已比较；无需额外代码改动。 |
| `Style::position` 字符串热路径 | `6c58cab3` | 保留原字符串用于兼容/诊断，解析时同步生成 `PositionType`；布局、flex 排序、layer 和 relative/fixed 判定改用枚举，并增加解析回归。需随主线 CI 闭环。 |
| `form_control_kind` 类型字符串规范化 | `f998c5be` | 类型关键字改为无分配 ASCII 大小写比较，保留默认类型及大小写不敏感语义；Render Core 全测试通过。需随主线 CI 闭环。 |
| radio group 校验重复遍历 | `845645e5` | `validate_form` 在首次遇到 required radio 时惰性收集已选 group；普通 form 不增加预扫描，required radio 从每控件重扫降为一次 group 扫描。Render Core 全测试通过。需随主线 CI 闭环。 |
| select option 重复遍历 | `51279fd4`、`b1e6c287` | 状态更新路径复用一次 option 快照；高级 popup 的绘制与命中路径也改为一次收集后按指针访问，旧的按索引 API 保留兼容。256-option 基准与 Render Core 全测试通过，需随主线 CI 闭环。 |
| NetworkFetch pending 响应重复复制 | `7e671eba` | pending 请求改存 fixture 索引，完成时只复制一次 response body/content-type；保留 fixture 可复用语义，并通过 App Runtime 全测试。需随主线 CI 闭环。 |
| Video pending fixture 生命周期假设 | 当前 `app_video_frames.cpp` | `PendingFrame` 保存请求源字符串，`source_pending()` 不再通过 pending fixture index 反查可变 fixture 数组，消除清理/重排场景下的越界假设；保持单 source 单 in-flight 语义。App Runtime 测试通过。 |
| 文本规范化的 layout→paint 重复路径 | 当前 `TextLayoutCache` 与既有回归 | 已核对：正常 layout→paint 路径使用布局阶段生成的 transformed text/lines，layer cache 校验通过后不再重新规范化或测量；脏文本复用判断必须针对新文本重新计算，不能复用旧缓存。当前不新增 Node 级缓存，保留既有测试覆盖。 |

### 仍开放，纳入后续工作

这些项目没有被上述提交完整关闭，后续应按收益和风险单独处理：

1. flex 非 wrap 的多次 intrinsic layout（报告 H6）：已补 `flex_nonwrap_intrinsic_layout` 基准；8/32/80 个柔性文本子项桌面 Release 约为 20/66/192 us，测量次数受探测/最终/拉伸分支影响。暂不做全局缓存，后续仅评估纯叶子文本等可证明安全的快路径。
2. app service 的 fixture/response 仍有少数非必要复制（报告 services #5/#7）：NetworkFetch 已由 `7e671eba` 处理；compute result 和 audio URL 已确认完成路径使用移动语义，不需要新增改动。视频帧、解码 surface 仍保留整洁的公开 `std::vector` 记录接口，因此像素载荷的共享/零拷贝需要单独的 API 设计，不在本轮以破坏兼容性的方式处理。
3. 文本规范化报告 M9 已完成评估，当前不需要新增代码；后续仅在文本缓存失效模型扩展时重新验证。
4. Low 级可读性条目和未逐条复核的历史条目：不作为当前发布阻断项，修改时必须附局部测试或基准。

### 2026-09-13 WS147 归档

`test_artifacts/ws147_panel_scroll_fix_review_20260913` 已完成 SHA-256 和选择性 patch 核对。普通 framebuffer scroll-blit 配置（`panel_scroll_mode=0`）在 WS147/JD9853/ESP32-S3 上通过实机视觉验收，未观察到底部错行，串口无 panic、watchdog、reset、DMA、SPI、panel 或 present 错误。GRAM fallback probe 仅为旧 SDK 构建的辅助行为证据；由于没有 TE/vblank 或等效扫描同步证据，也没有 GRAM 视觉验收，因此 `productionPanelScrollAccepted=false`，该实验路径不进入主线生产默认配置。

### 2026-09-14 WS147 Render Performance A/B

`D:\JellyFramePerf\comparison-13264-de0c541d\matrix` 已完成四个 workload 的定量矩阵：`static-local-repaint`、`text-layout-update`、`drag-scroll` 和 `full-repaint` 均完成 3 次 baseline/candidate、Profile ON 与 OFF 采集。基线为 `a58f89ff`，候选为 `de0c541d`（包含主线 `13264bcd` 及 ESP32-S3 GCC 13.2 有界排序修复）；四组均为 0 error signature、0 present failure。候选 frame p95 在四组分别为 `-35.71%`、`-36.36%`、`0%`、`-31.18%`，`full-repaint` 的 present p95 为 `+2.94%`。

该矩阵当前仍为 `PARTIAL`。操作者已补充 12 张照片，归档清单中的 SHA-256 全部匹配，人工结论为四组 `visual-equivalent-only`；未观察到候选新增视觉回归。照片缺少 EXIF 时间和 baseline/candidate/phase 映射，且拍摄角度与光照不同，因此不能升级为固定时刻或像素级视觉等价证据。`drag-scroll` 两侧共同存在绿色文字下沿裁剪，属于共享 fixture 限制，且该 workload 是合成双向拖动，不等同于真实触摸 input-to-present 延迟；不得据此关闭 A2 panel/input 出口。

### 验证出口

- CI：`75a550a6` 的运行必须完成，重点查看 sanitizer、Windows scripting、standalone Render Core consumer 和 documentation freshness。
- 桌面：当前本地 Release 全套 `50/50` CTest 通过；新增性能改动不得降低既有文本、圆角、flex 与 clip 回归覆盖。
- 设备：panel-scroll 实验文件仍不入主线；只有 TE/vblank 同步、真实 input-to-present 和恢复证据齐备后才重新评估。普通 framebuffer scroll-blit 归档可作为当前安全路径证据。
- 任何“已修复”项在缺少对应 CI/设备证据时只能标为“代码已落地，验证待闭环”。

---

## 原始建议的修复顺序（历史记录）

这里保留最初的建议，便于与审查原文对账；其中多数已由 `e9775ef4`、`d3cbbf56` 等提交处理，当前执行顺序以本节上方的处置状态和验证出口为准。
