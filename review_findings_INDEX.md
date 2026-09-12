# jellyframe 0.6.1 代码审查 — 汇总与交叉核对

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

## 建议的修复顺序

先做模式一里的 `std::move` 改造（`app_compute_jobs.cpp:185-190`；`app_services.cpp:501`、`559-566` 的 `NetworkFetchRecord`、`1095-1104` 的 `AppDecodedSurfaceRecord`、`1339-1346`），改动都在一两行内、无所有权语义变化，收益直接落在最大的缓冲区上。随后处理模式二的三处 `remove_if` 合并，照抄同文件已有写法。模式三需要引入缓存与失效点，改动面较大，放在最后，且应先用现有 benchmark 量化收益。`app_budget.h:105` 的槽位数与 `static_assert` 是一行改动，可以顺手带上。
