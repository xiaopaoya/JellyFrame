# JellyFrame 活动待办

> 最后更新：2026-09-19；适用版本：0.6.0-dev
>
> 本清单是 [路线图](roadmap_zh.md) 的近期执行队列，不记录已经关闭的验收、性能微实验或历史移植任务。

## 现在：更宽范围 A2 产品出口与 B1 边界维护

当前执行顺序固定为：先处理已确认的安全/正确性缺陷，再完成性能观测闭环，
最后推进设备出口和体验增强。任何性能或编辑器工作都不能替代 A2 的实机证据。

### 本阶段未来四轮对话计划

1. **审查关闭矩阵**：逐条回读两轮审查和新性能审查，记录“已修复、已验证、误报/不成立、
   延后 RFC、待修复”及对应测试/实机证据；不重复修改已经关闭的项目。
2. **剩余边界修复**：优先处理仍成立的公共 dirty/clip 上限、跨任务资源身份和资源计费边界；
   每项同时补正常、拒绝、异常和重复生命周期测试。
3. **性能缓存与基准**：在稳定 workload 下评估 DOM 统计、字体 fallback context、图像缓存预算、
   dirty invalidation 的收益；没有 workload 证据的全屏微优化继续延后。
4. **出口与发布准备**：复跑 Debug/Release/scripting/tool 全套门禁，审查矩阵归档，确认 Core
   `0.6.2` lock、Developer Image、SDK 和 A2 实机证据一致后，再处理 A3 试用材料。

当前轮次已完成审查矩阵的初步回读、低风险修复，以及 WS147 定量 Profile OFF/ON 与人工视觉等价验收；下一轮只进入矩阵中仍标记为“待修复”或“待证据”的项目。本候选因缺少治具豁免逐像素比较，该豁免不建立自动像素门禁。

### 1. 审查报告治理（当前最高优先级）

- [x] 逐条评估两轮审查产生的漏洞/鲁棒性报告：`review_findings_INDEX.md`、
  `render_core_code_review.md`、`review_findings_render_core_rest.md`、
  `review_findings_app_runtime_services.md`、`review_findings_app_runtime_lifecycle.md`、
  `review_findings_app_runtime_script_task.md`。每条标记为修复、误报/不成立、延后 RFC
  或需要实机证据；不能只按报告标题假定成立。
- [x] 优先关闭 P0/P1：特权服务授权、资源/句柄生命周期、dirty/布局预算、viewport 单位、
  文本测量和跨任务完成语义；每项必须有正向、拒绝/异常和重复生命周期回归。
- [x] 对新一轮性能审查中的 P1/P2 逐条回读源码，避免重复修复已经关闭的项目；仅在有稳定
  workload 或明确正确性收益时修改热路径。
- [x] 更新审查索引和项目状态，记录每条发现的证据、提交、测试和剩余风险。

### 2. 渲染性能观测闭环（独立并行主线）

- [x] 修复重复失败的 scripting CI：`08dc32ba` 将非 trace capture 的诊断重绘预期
  修正为 0，新增 trace OFF/ON 分别 paint=0/1 且 BMP 完全一致的回归；干净 scripting
  Release 全套 82/82 通过，远端 CI `35451942516`、后续工具提交 CI `35452510254`、`35453618347` 四项 job 均全绿。
  无需恢复无条件诊断重绘来满足过时测试。
- [x] 完成原生 GDI 圆角资格检查：Core 通过独立 4x4 coverage oracle；固定卡片与
  GDI RoundRect 有 392 个不同像素，输出明确为 not-comparable，无计时数据。
  自动化回归与比较器拒绝资格报告的测试已加入；本项不关闭等价 AA adapter 待办。
- [x] 完成 GDI+ 超采样圆角资格检查：8 项固定几何/裁剪用例均通过 Core 独立 oracle，
  GDI+ 仅普通矩形对照通过，其余 7 项不满足 exact coverage。报告比较颜色量化前覆盖率，
  无计时；增加空白、平移、圆角丢失、越界着色与无效调用回归。不关闭等价 AA adapter 待办。
- [x] 交付不同原生 AA 的独立实验质量合同、mask 导出和 Python 分析器：参考连续像素面积
  的上下界，不以 Core 为理想答案；内部/外部检查、边缘 RMS/最大误差、面积偏差和
  pass/fail/indeterminate 状态均可复核。Core 与 GDI+ 当前配置均有未通过项，二值负对照
  被拒绝；保留原始 mask/hash，不修改历史 exact 合同或设备发布门。
- [ ] 下一步接入独立的原生 AA 质量/成本联合报告，先冻结实际执行与完成同步计时边界。
  未通过质量门只保留诊断，不进入等价排名；不得为完成基准默认修改 Core 像素网格。
  通用文本/shaping、完整 UI 和 LVGL 对照仍单列待办。
- [x] 修正半透明对照的计时边界：`a4faa1c6` 在计时内完成 SDL2/GDI batch，
  在计时外完成 reset，并交错执行两侧；`alpha-grid-rgb-v1` 的旧排名已撤回。
- [x] `1e375e5b` 为不透明目标增加逐字节等价的 source-over 快路径；Release/Debug Core
  回归及 16,777,216 组单通道组合通过。修正后的桌面基准每侧 6 轮、每轮 500 样本，
  JellyFrame 的 batch-average 单 tile p95 中位数下降约 15%–17%，不代表设备或整体 UI 加速。
- [x] 实机复核 source-over pair：baseline `a4faa1c6` / candidate `1e375e5b`。
  `D:/JellyFramePerf/source-over-opaque-20260919/hardware/` 的 24 个窗口、固件/日志/
  配置哈希已核验；用户确认目检正常，四 workload 比较器均为 PASS (visual-equivalent-only)。
  frame p95 均持平，paint 变化仅一个 1 ms 桶，不宣称设备显著加速；该非回归门已关闭。
  仍是 aggregate-only、缺失 script_us、pipeline_frames=0，不代表完整 UI 管线或逐元素性能验收。
- [x] 将桌面多轮对照收敛到 `benchmark_compare.py`：支持每侧 3–32 轮，校验跨轮条件与
  单侧版本稳定，保留逐轮 p95、其中位数/范围和输入哈希；source-over 六轮存档复算一致。
  不合并采样、不自动判定显著性；设备继续使用独立 aggregate 比较器。
- [x] 新增 `bitmap-clock-text-rgb-v1`：共享固定字形，Core BitmapFont painter 对照 GDI
  缓存字形 blit，独立 mask oracle 与全屏 exact RGB 验证；Debug/Release 回归和六轮
  500 样本测量完成。只关闭固定 bitmap 绘制子集，不宣称通用文本/shaping/换行或设备加速。
- [x] 完成现有 Render Trace/Performance Profile 的状态核对：桌面逐帧 trace、阶段/command/owner
  归因与设备 aggregate profile 已交付；设备逐元素 trace 尚未实现，aggregate 数据不得冒充元素耗时。
- [x] 用同一 App、输入、构建和机器完成 profile OFF/ON A/B，确认 p50/p95、报告开销
  和 trace I/O 边界。版本化 WS147 矩阵包含四个 workload、每侧三次重复，错误签名与
  present failure 均为零；像素等价仍需独立的固定时刻视觉清单。
- [x] VS Code Render Trace 查看器已具备阶段占比、dirty overlay、最慢帧跳转、逐帧正确 Top-64
  command 排名、缺失截图/阶段/命令状态，以及明确的 `unattributed`/截断提示；不把 aggregate 数据伪装成元素耗时。
- [x] 已在 VS Code Render Trace 查看器交付跨帧 command 聚合；独立 Device Performance Profile 查看器仍需
  评估。继续保持静态报告与逐帧 trace 分离，不能把设备 aggregate telemetry 伪装成逐元素数据。
- [x] 在真实 developer-image workload 上完成静态、文本、合成拖动/滚动和全帧四类 Device Profile
  窗口；设备结论记录于版本化 `comparison-13264-de0c541d/matrix` 报告。合成拖动不等同于
  真实手势延迟测量。
- [x] VS Code 内嵌调试已增加显式启停的桌面 Render Trace；默认不采样，采集时使用
  600 frame / 4 MiB / 4 KiB 单行上限的环形缓冲，停止后原子写盘并打开现有查看器。
  整页 `present-only / scroll-blit` 与内部 `scroll-container` 已分别补齐真实 present 及
  layer/paint/present/command 证据；设备侧仍保持 aggregate-only。
- [x] 归档并校验四个 workload 的操作者视觉复核。12 张照片的 SHA-256 全部匹配，支持
  人工视觉等价且未报告候选特有回归；候选于 2026-09-15 获准通过。因缺少 display-readback
  仪器或治具，本候选豁免逐像素比较，该豁免不建立自动像素门禁。拖动 fixture 两侧共同存在
  绿色文字下沿裁剪，继续单列限制。
- [ ] 继续扩展与主流图形库的同分辨率/像素格式对照。当前 GDI 已覆盖全屏/局部不透明填充和两种轴向渐变，
  GDI/SDL2 software renderer 均覆盖 exact-RGB 的半透明 source-over 网格，SDL2 另覆盖 exact-RGB 的
  全屏/局部不透明填充；固定 bitmap 文本另有 GDI glyph-blit 对照，圆角、通用文本、完整 UI 与 LVGL 仍无满足
  等价语义的 adapter。在这些矩阵完成前不宣称“JellyFrame 整体比主流图形库快/慢”。

### 3. 近期交互性能与设备出口

- [ ] 完成 [WS147 Panel Scroll 同步修复与移植验收](../docs/ws147_panel_scroll_te_acceptance_zh.md)：先确认 JD9853 实际 TE 能力并修复 VSCSAD/strip 时序；在相同固件/profile 下复核 input-to-DMA-complete p50/p95；
  当前自动滚动 profile 只能证明 render-to-present 路径，不能证明真实手势延迟。每个真实输入必须关联 sample、
  dispatch、mutation/frame、paint-end 与 DMA-complete；只有加入 TE/latch 或光学测量后才能声明 input-to-present。
  若未形成证据，继续处理输入采样、队列合并、脚本更新和重绘范围，而不是凭手感扩展功能。

- [ ] 在干净作者机完成 WS147 VS Code 的只读 smoke：发现、身份读取和已安装 App 列表必须与 manifest/registry 一致。要求见 `ws147_provider_vscode_smoke_20260825_zh.md`；该项不执行安装或刷写。此前本机 candidate smoke 不可替代此项。
- [ ] 在同一干净作者机完成 VS Code 设备流程：`new -> check -> package -> deploy -> launch -> live log -> update -> rollback -> stop -> remove`。桌面与设备 session 必须保持独立，最终报告必须保留可定位的失败归属。
- [ ] 通过 provider 流程完成真实已安装 App 的 panel/input 验收。2026-09-08 用户补充观察称实机操作响应正常，但归档中的 `posted=0` 采样不是结构化输入证据。仍需按 App 记录 launch marker、触控/输入响应、panel/present 错误与恢复行为；provider lifecycle PASS 或非结构化观察不等于完成视觉/输入验收。
- [x] WS147 物理 GRAM panel-scroll 已完成性能与 fallback 复测，但视觉验收失败；已由 `eaa52a67` 增加视觉接受门并从默认可用路径隔离。后续仅调查 TE/vblank 同步，不得重新启用发布配置。
- [ ] 将 B1 作为持续 release gate 维护。带签名的 Core `v0.6.2` release 是当前 Runtime 依赖，Runtime 锁定 Core `0.6.2`、ABI `1` 和 source identity；以后每次 Core bump 必须下载或以其他方式认证已审阅的 release artifact、校验 archive SHA-256、更新精确 version/ABI/source lock，并通过 standalone、package-consumer 与 source-override tests。
- [x] 基于已合入的 Runtime Core `0.6.2` lock 构建并验收 WS147 Developer Image `0.6.2-ws147.1`。历史 `0.6.1` manifest 与证据保持不可变；R1-R17、host/provider 与 package-smoke 完整报告为 `core062-developer-image-final-20260905`，物理 Developer Image gate 已关闭。
- [x] 从 Runtime `ca747011` 发布 App Author SDK `app-sdk-v0.6.0-dev.2`；标准与 scripting 桌面运行时均消费 Core `0.6.2`，release archive SHA-256 为 `c3245edd...7dc8a9f`。
- [x] 完成 [0.6 工程维护审查计划](engineering_review_plan_20260819_zh.md) 中已安排的 R0/R1 首轮检查：package/profile/provenance、style/layout/dirty、renderer/text 和确定性 capture 均有对应回归；只修复有明确语义或安全缺陷的接口，不做机械式改名。
- [x] 完成响应式布局基础后的 R1 Core-only 首轮审查：`300x300`、`320x240`、`172x320` 矩阵及 Flex 交叉轴回归作为持续 gate；parser/style 所有权、malformed-input budget、cache invalidation 与确定性 capture 的剩余设计问题已转入 RFC，不以新增控件或浏览器专有声明替代。
- [x] 关闭 2026-09-07 定向审查发现：Flex sizing 跳过约束未变化的 probe layout，通用 worker 保留非零 `client_token`，worker pipeline rebuild 静默恢复 autofocus 状态而不重复派发 `focus`，worker layout 使用宿主 budget。Debug、启用 scripting 的 MinSizeRel 与 ASan/UBSan 回归均通过；审查 probe 复核当前基线的 `anywhere` 测量为线性。
- [x] 关闭后续 callback/service 审查项：timer pump 期间延迟清理，回调中新建 timer 不会改变当前批次顺序；rAF 在实际轮到执行前保持可取消，并在批次结束后回收；排队中的 XHR 取消会释放 provider 持有的 fixture 副本，已被 worker 取走的请求继续使用迟到 completion 回收路径。Debug 与 scripting MinSizeRel 定向测试通过。`client_token` 已由上一项覆盖，不重复计数。
- [x] 关闭 2026-09-07 性能/行为复核中的低风险项：动画 override 按节点建立索引；内置字体字间距复用 UTF-8 scalar 测量；具备 additive measurement 契约的 provider 使用增量换行宽度；CSS class 候选索引使用稳定 `string_view`；单个 rAF 异常不会丢弃同帧后续回调。未知字体 provider 仍保留整段测量语义，成员所有权转移处保留 `std::move`。
- [x] 完成 2026-09-10 低风险维护批次：LayoutBox 向 layer generation 复用文本归一化/换行结果；圆角裁剪与内置 bitmap fallback 跳过重复工作；dirty rect 合并使用有界的 generation 候选；layout/paint 共享 flex 排序 helper；变换栅格与 opacity 路径提取不变量并避免复制命令字符串；Render Trace 查找一次性快照目录。Release、scripting 与工具回归均通过。
- [x] 完成软件栅格器首轮极值安全审查：圆角 coverage 的距离平方、clipped 几何循环、描边内框和 BMP 导出尺寸使用 checked/saturating 计算；完整与响应式 Core profile 均通过 `9/9`。精简 profile 的聚合 CTest 不作为失败依据，因为该 profile 不生成桌面壳和 pseudo browser 目标。
- [x] 完成文本路径极值审查：fallback 字体测量、字间距、anywhere wrap 和内置 bitmap 绘制定位均使用有界计算，并覆盖最大字号/间距回归；未改变正常字号布局语义。
- [x] 收敛布局定位极值：flex wrap、inline flow、文本总高度、flex 汇总/对齐和递归 `shift_box` 使用 bounded 算术，换行行数转换有明确上限；完整与响应式 Core profile `9/9` 通过。
- [x] 收敛 grid/positioned 极值：列/行 track、gap、跨轨道分配、绝对定位 inset 推导、坐标偏移和 fallback 排布使用 bounded 算术；完整与响应式 profile 均通过 `9/9`，未改变正常尺寸语义。
- [x] 收敛 layer-tree 绘制边界：多行文本的行号/行高、逐字 letter-spacing cursor、文本装饰线与 outline extent 使用有界坐标计算；完整与响应式 profile 均通过 `9/9`。
- [x] 修复 CSS nesting 预算语义：深度与展开字节预算为 `0` 时按契约表示不限制，并对 selector 组合数量使用无溢出上界判断；新增零预算回归，完整与响应式 profile 均通过 `9/9`。
- [x] 修复 CSS `var(...)` malformed 分支绕过 resolved-value 字节预算的问题；空变量名片段统一使用有界追加并增加超预算回归，完整与响应式 profile 均通过 `9/9`。
- [x] 收敛 compositor 的 offscreen 失败回退：变换或圆角裁剪无法分配临时 surface 时跳过不正确的直接绘制，避免未变换或未裁剪内容进入目标帧；新增圆角预算回归，完整与响应式 profile 均通过 `9/9`。
- [x] 将 R1 审查记录、预算契约 RFC 和 `wrap_text_anywhere` 长度 benchmark 整理到主线；RFC 仍不改变公共 ABI/API，benchmark 仅作为后续等价优化的诊断基线。
- [ ] 维护 B2 脚本运行时边界：通用 host 与 worker 代码只能包含 `script_runtime.h`；引擎 headers、value 和发现逻辑必须留在选定后端内。未经过独立批准的 RFC 与对等证据前，不引入第二后端，也不改变对外开发者口径。

## 并行：A3 内测筹备

状态：**进行中，由外部协作方负责宣传与筹备对接。**

- [ ] 准备最小试用包：已发布 Developer Image/provider、VS Code 扩展安装说明、`blank` 模板起步流程、已知能力边界与支持渠道。
- [ ] 固定反馈归档格式：App `.jfapp` 或源码包、image/provider/extension 版本、复现步骤、JellyFrame Output、设备 logs 与是否可复现的最小 capture。
- [ ] 准备首轮筛选与响应规则：安装、运行、恢复、数据损坏和文档化能力不符为 P0；不把未声明 Canvas、全屏 30 FPS 或完整浏览器 API 作为缺陷承诺。
- [ ] 按 [可视化 App 编辑器计划](visual_app_editor_plan_zh.md) 完成阶段 1；随后只补 generated-region 冲突保护和真实桌面壳交接所需的阶段 2/3 最小切片，再决定是否作为内测宣传功能。

这些筹备项不放行实际外部试用。只有上方两项 A2 正式证据和 panel/input 验收关闭后，才可分发访问与收集产品可用性数据。

## Core 发布后的候选能力

新的 Render Core 能力仅在独立治理的 Core release line，或已批准的同一拆仓 release window 内开始。每个候选都需要可复现的作者需求、RFC、正/负行为测试、三 target desktop capture、能力矩阵/诊断/recipe 更新和热路径 benchmark。

- [ ] 在独立 Core line 重新评估并完成 `text-wrap: balance` 的 candidate evidence（当前 `0.6.2-dev` 未声明该能力）。完成前不得写入 Runtime 作者能力矩阵；之后还需由 Runtime 明确选择 package/default-provider integration，并单独评审 lock 更新。
- [ ] 只有在可复现作者需求与 RFC 明确 feature、profile impact 和 hardware budget 后，才能选择下一个 Core candidate；不得默认重新开启广泛 CSS 兼容性工作。

核心侧只在需要新增平台无关 contract 时介入；不得以 reference endpoint 伪造实机完成。

## 明确不进入当前队列

- [ ] 不继续 full-frame rounded/gradient 的 copy、span、DMA 等微优化；只有真实 developer-image workload 的 telemetry 能重新开启性能项。
- [ ] 不启用 retained replay、framebuffer reuse 或 tile/scanline renderer，除非先满足路线图中的独立证据门槛。
- [ ] 不将 Canvas、完整 SVG/video、Shadow DOM、Worker、iframe、`:has()` 或容器查询作为 `0.6` 默认范围。
- [ ] 不开始外部硬件开发者试用，直至更宽范围 A2 的干净机器与 panel/input 出口通过。

## 每项最低检查

- [ ] `git diff --check` 和相应 Debug/Release CTest。
- [ ] scripting、工具、package 或 profile 改动的定向回归。
- [ ] 热路径改动的 focused benchmark；硬件结论的版本化 port 报告。
