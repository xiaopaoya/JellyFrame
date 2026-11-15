# Render Core ABI4 集成计划

> 最后更新：2026-11-16；适用版本：0.6.0-dev。Core 已发布版本：0.9.0 / ABI4。

> **最新检查点（2026-11-16）：**Runtime PR #99–#113 已合并；#111 将 WS147 band-shell CoreText profile 设为默认，并合入 Win32 ABI4 属性与增量/全量重绘矩阵（293 帧、27 组流程）。`mainline1` 实机验收已记录，不重复。Core PR #67 已以 `16a83211ec0992f8e25ec0fc977eb6fd158716e3` 合入，ABI4 契约完成，Core CI 与干净 Release CTest 通过。Core v0.9.0 / ABI4 的签名 tag 与 GitHub Release 已发布；归档及 `.sha256` sidecar 均已上传，归档 SHA-256 `8a0568b548a7e30956c515bba753ad7fe7f0fbb501e319cbef926eec2b855628` 与 GitHub asset digest 一致。Runtime 锁更新候选分支 `codex/core-0.9.0-abi4-lock-20261116` 基于 master `2e116182`，已有锁、app manifest/模板、provenance CI 和 ABI4 installed consumers 改动；full/cropped 本地 CTest 各 22/22 通过，package/profile 回归也通过。补充审计发现三个 ESP32 探针生成器仍输出 Core 0.7.0，候选中已改为 0.9.0 并验证生成包。用户已在 `0.6.2-abi4-installed-core63-device1` 上通过 `Core 63 Select Popup` 实机验收，标题、四个选项、结果与触控正常；JFDP 首帧接受且 render/present 失败为 0。Runtime master 在候选 PR 通过 CI 并合入前仍锁定签名 Core 0.8.0 / ABI3。`mainline2` 仍是另一独立待验收 profile。

本文件是当前执行计划。旧检查点保存在 Git 历史和对应 PR 中，不再作为额外待办。
[文本消费者退役清单](render_core_text_consumer_retirement.md)记录实际调用方与原生设备候选的完整身份。

## 当前检查点

| 边界 | 当前状态 | 剩余门槛 |
| --- | --- | --- |
| 已发布依赖 | Core v0.9.0 / ABI4 签名 tag 与 Release 已发布；归档和 SHA-256 sidecar 均已上传，远端归档 digest 与本地一致。 | Runtime 锁更新 PR 通过 full/cropped、ABI3 source-override 与其余 CI 后合并。 |
| Core 主线 | Core PR #67（`16a83211ec0992f8e25ec0fc977eb6fd158716e3`）已合并，ABI4 契约完成，Core CI 与干净 Release 检查通过；v0.9.0 签名发布及归档资产核验完成。 | 无；后续由 Runtime 锁更新 PR 消费已发布包。 |
| Runtime 主线 | PR #99–#113 已合并。候选分支 `codex/core-0.9.0-abi4-lock-20261116` 更新精确 package lock、app manifests/模板、provenance CI、ABI4 installed consumers 和三个 ESP32 探针 manifest；full/cropped 本地 CTest 各 22/22 通过。 | 推送并开启 PR；CI 通过后合并，届时 Runtime master 才切换至 ABI4。 |
| Embedded host | #100 为 layout/layers/paint 绑定同一个 TextSystem/JFFONT2；锁定 ABI3 和 source ABI4 full/cropped 测试通过。 | 该示例不再依赖文本 provider 回调。 |
| Installed worker | 已合并 #101，让无 painter 的 canonical v10/CoreText 独立于 BMP 工作。#107 覆盖 Core #63 popup 输入、prepared packet、行像素、选择与关闭；#109 增加 full-feature WS147 profile。Core #65 generated pseudo-content 已纳入 Runtime source-override build/CTest。用户已在 `0.6.2-abi4-installed-core63-device1` 上确认标题、四个选项、选择结果与触控正常；JFDP 首帧已接受且 render/present 失败为 0。 | popup named profile 的用户可见验收通过；仍需退役其余 provider/painter 路径。 |
| 原生 WS147 | `0.6.2-abi4-mainline2` 已基于 Runtime #103/Core #62 构建；app BIN 7,614,704 B，8 MiB app 分区余 773,904 B，16 MiB factory/包哈希已核验。另一份 `mainline1` 于 2026-11-15 通过页面观察。 | 仍需验收精确的 `mainline2` 产物；`mainline1` 不能替代该镜像或 installed worker/BMP 的测试。 |
| ABI5 与 shaping | 独立可选候选；ABI5 tracked reuse 有正确性与包测试，尚无合格性能收益。 | 未启用到 Runtime，也不阻塞 ABI4 发布。 |

实验源码声明的是 0.6.2 / ABI9。版本号或合并 PR 数量都不能证明迁移完成；
每项能力还需要受治理的 Core 契约、Runtime 消费者、资源交付和适用测试。

## 并行工作线

### A. 资源交付与默认字体目录

已完成签名包离线 bootstrap（#73）、共享 Noto Sans SC JFFONT2 字体目录及所有权 API
（#75）、显式桌面/设备目录路径（#76/#77），以及后端/源码身份、路由和重绘验证
（#79–99）。Core #60 提供有界 letter-spacing 和 JFTP v3 显式位置，仍能解码 v1/v2。
规范路径由 Core 负责选择、度量、准备和栅格化，移植者只注册不可变字体资源。

已由 Runtime #103 完成：删除 JFFONT0/1 序列化路径与 AppFontSet 回调优先级，
manifest/import 统一为 JFFONT2 并使用 Core 离线生成器。本地移植改动已拆分 ESP32
构建边界：CoreText 构建不再生成或链接旧 Noto C++ 位图包；CoreText 关闭的构建和明确
选择旧 bitmap painter 的验收 fixture 仍保留该包。ESP-IDF 5.3.1 下多个配置均构建成功；
CoreText band-shell 和 P4/P5/P6 smoke 的 map 中没有旧字体符号，legacy profile 则生成并链接 27 个 face。
这两个 BIN 使用不同 sdkconfig，不能直接把 BIN 差值视作字体包净开销。剩余：将 CoreText
资格提升为默认，迁移/退役其他 generated-font 消费者，并保留确定的 family/weight 选择、
字体字节寿命、缺字拒绝和资源预算；不增加新的宿主文本回调。

### B. Runtime 消费者迁移

| 消费者 | 进展 | 完成证据 |
| --- | --- | --- |
| Embedded host | #100 完成。 | 可见文字、click/checkbox/select、RGB565 flush；ABI3 与 ABI4 full/cropped。 |
| ESP32 installed worker | 已合并 #101：无 painter 的 canonical v10；目录/快照覆盖重放寿命，BMP 可选。 | 主机 full/cropped、锁定 ABI3、真实 IDF 构建、CI，随后精确设备 profile 验收。 |
| ESP32 retained native/default | Runtime #111 将 WS147 band-shell profile 默认设为 CoreText；profile guard 与精确 Core #63 ESP-IDF 构建通过，legacy typography acceptance 仍显式隔离。 | legacy typography acceptance 保持隔离；后续设备验收需绑定命名镜像，已验收的 `mainline1` 不重测。 |
| ESP32 P4/P5/P6 smoke | `main.cpp` 中独立 provider 仅被 benchmark/smoke fixture 使用；CoreText profile 现在已有单独 CoreText smoke 构建证据。 | 保留旧 fixture 为显式 legacy-only，并让 CoreText smoke 覆盖同等 ASCII/CJK/字重和控件场景；它不是产品默认消费者。 |
| Win32 默认路径 | 显式 CoreText 目录模式已合并，默认仍为 GDI。#110 修正 CoreText 文本变化的布局复用资格；#111 合入 ABI4 属性/重绘矩阵，293 帧、27 组流程通过。 | 在 API/fixture 退役后决定是否将 Core catalog 设为默认；GDI 当前仍是明确可选路径。 |
| AppFontSet | #103 已完成：目录仅接受 JFFONT2，移除回调访问器/浏览器优先级，并转换样例和工具。 | AppFontSet 无剩余工作；generated C++ BitmapFont 默认资源另行跟踪。 |

Canvas2D 已使用 TextSystem，不是剩余回调消费者。Installed worker 候选在写帧缓冲前
拒绝依赖回调的普通 `Text` 命令；这是有界准入规则。Core #65 现将 generated
pseudo-content/list-marker 的 fallback 交给 TextSystem 准备或明确拒绝，不再发出旧
Text 命令。Runtime 仍需对可达这些 producer 的路径验证精确 Core #65 源码，并清点其他
回调消费者和仅由测试/fixture 使用的旧路径。

已合并 #101 的 worker 主机证据：Core PR60 full 和关闭四类可选能力的 cropped 均通过 7/7；
BMP OFF/ON 都覆盖可见 prepared text、JavaScript 文字修改、退出后的重放和原子拒绝。
锁定的已安装 ABI3 包通过四项 admission 测试，两项 canonical 测试按预期跳过。
CoreText OFF 的 legacy 路径保持显式选择。

### C. Core 文本 API 收敛

Core #61 已在四项 CI 全部通过（run `43670844631`）后合并为 `3152300a`。有界 owner-side ellipsis 的 MSVC full 测试 33/33、已安装 full/cropped 文本消费者各 1/1 通过；cropped 唯一 shared-inline 阴影断言失败在未修改基线同样复现。Runtime #102 已合入该精确源码 pin、五帧真实字体增量/全量省略号测试和 clip-only 可见对照。Core #62 为闭合原生控件 label/select arrow 提供 Core 度量与事务式 prepared packet，已合并为 `38148e73`。Core #63（`89acc36a`）在发布 popup overlay 前准备所有可见选项；缺字或预算拒绝时整个 overlay 都会拒绝。Core #65（`d327eee4`）将 generated pseudo label/list marker fallback 迁移到 Core TextSystem 准备或明确拒绝，四项 CI 全通过。Runtime 仍需在可达这些 producer 的路径验证精确 #65 源码，并继续盘点更广的回调 API/fixture 退役范围。实现必须保留源文本布局/固有尺寸，在最终几何
确定后按 owner 裁剪，使用 Q1000/spacing 和输出预算，并对不支持的完整上下文拒绝，
不回退宿主。混合 owner 需要整段截断所有权，不能各叶节点独立截断。

所有生产消费者迁移后，集中删除 `TextMeasureProvider`、`TextPainter`、range/family/
additive 接口，以及 layout、intrinsic、cache/repaint、layer emission 和 rasterization
回调分支。同步转换测试、benchmark、包消费者和移植指南，避免保留早期兼容别名。
当前只删除了未使用的 HostTextAdapter 包装层。

完成门槛：生产路径不存在回调消费者，完整 Core、源码归档、已安装 full/cropped 消费者
及关闭能力矩阵通过。实机是下游采用门槛，不应成为保留死接口的理由。

### D. 发布和生产锁

Core v0.9.0 / ABI4 已从最终审查通过的源码发布并签名；tag、归档 checksum、source
manifest、profile 和 Release 资产均已核验。Runtime 精确版本、ABI、源码/归档锁及
已发布产物 CI 已在候选分支更新，等待 PR CI 后合并。ABI3 作为回退证据保留，不能
声称 ABI3/ABI4 二进制互换。

### E. 设备准入

原生候选位于 `D:/CodexTemp/abi4-mainline-20261115`，固定 Runtime `75a435c5`、
Core `34ae4b88`，使用干净的 IDF5.3.1 及递归固定的子模块。入口为 `/band_shell.html`，
CoreText ON，BMP/installed scripting OFF；BIN、包/ZIP 和 factory app 段逐字节一致。
设备身份使用启动 app version 和 ELF SHA，完整 hash 见消费者清单。

单独测试的 `mainline1` 身份为 Runtime `75a435c5`、Core `34ae4b88`，BIN SHA-256
`AFB9F47E015FF5EB06AD441EBC322F50B04D6149CFA3E70B8C96675EF46C696E`，ELF SHA-256
`B462F78731662B53A21DEAEE1F86E4E0995F37D2F964BEDDA3D27B284FA522910`。启动日志确认
`/band_shell.html`、CoreText 240 MHz、触控检测和首帧呈现成功。用户确认首页文字完整清晰，
Activity 与 Weather 文字显示正常，Weather 下方四个小卡片均有文字，页面返回正常。该结果只
关闭此 native 镜像上的观察项，不覆盖独立的 `mainline2`、installed worker/BMP 或 P4/P5/P6
benchmark smoke。此前的小卡片缺字反馈仍适用于其他未测身份。`mainline2` 实机验收仍待完成；
Installed worker/BMP 必须使用独立命名镜像和资源配置。Core #63 popup profile `0.6.2-abi4-installed-core63-device1` 已于 2026-11-15 通过用户观察：标题、OPTION、选择框、ALPHA/BRAVO/CHARLIE/DELTA、`SELECTED: CHARLIE` 均正常，触控正常；JFDP 首帧已接受，render/present 失败为 0。此结果不扩展到其他 Runtime/Core artifact。

## 依赖顺序

```text
资源契约/目录 -> native + worker + desktop 消费者
                              |
                         删除回调 API
                              |
                    签名 ABI4 + Runtime 精确锁
                              |
                    命名设备 profile + 实机验收
```

主机实现/审查、安装包验证和设备构建准备可以并行。默认采用遵循属性和资源门槛。
可选 ABI5 reuse 与生产 shaping 继续作为独立工作。

## 紧接着的工作

1. 已完成：Runtime #111 合入 WS147 CoreText 默认 profile，以及 Win32 293 帧、27 组流程的属性与增量/全量重绘矩阵。
2. 已由 Core #65 完成：generated pseudo label/list marker 使用 Core TextSystem 准备或明确拒绝；Core 四项 CI 全部通过。
3. 已完成：Runtime 使用精确 Core #65 identity `52ae4354...7302f62d` 完成 Release build 与 CTest 60/60；source override 测试配置也已链接 Core test hook。这证明 host 集成，不代表签名包锁定或设备验收。
4. 完成生产、测试和 fixture 对回调 API 的消费者清点，并决定 Win32 是否继续以 GDI 为默认；随后在协调预发布变更中移除 provider/range/family/additive API 和实现分支。
5. 最终 API 审查、full/cropped、禁用特性、installed consumer 与源码归档门槛通过后，签名发布 ABI4 并更新 Runtime 精确生产依赖锁。
6. 需要时分别跟踪 `mainline2` WS147 和 installed-worker popup 专用镜像；`mainline1` 验收已完成，不重复进行。

### 回调 API 退役执行切片（2026-11-15 审计）

Core `1d5c746d` 与 Runtime PR #112（提交 `8e833b35`，merge `59f61c40`）核对后，确认这不是只删几个类型声明的清理：provider identity 参与布局缓存有效性，LayerTree 仍会生成回调型 `Text` 命令，SoftwareRasterizer 仍能通过 `TextPainter` 或内置 bitmap fallback 绘制。以下切片按接口依赖排序，完成前不得删除公共声明：

| 切片 | 核心文件边界 | 必须达到的结果 |
| --- | --- | --- |
| 1. Layout / intrinsic / repaint | Core `layout.h/.cpp`、`intrinsic_source_query*`、`text_layout_reuse*`、`text_repaint*`、`style_repaint*` | 正常文本度量、换行、grid intrinsic 和 text-dirty reuse 只读取绑定的 TextSystem / Core 缓存身份；无 catalog、缺字或超预算必须明确拒绝。删除缓存中的 provider/context 身份。 |
| 2. LayerTree producers | Core `layer_tree.h/.cpp` 与 prepared-text/shared-inline producer | 不再读取 `LayerTreeBuilderOptions::text_measure`，不发出普通回调型 `Text` 命令；所有支持的文本使用 PreparedText，其他情况事务式拒绝，不回落到 host 或 bitmap renderer。 |
| 3. Raster consumer | Core `software_renderer.h/.cpp` | 移除公开 TextPainter 构造参数、成员和调用；保留 Core TextSystem 对 PreparedText 的绘制。对遗留 Text 命令做确定性拒绝且不写目标。 |
| 4. Runtime consumers | `tools/native/win32_browser.cpp`、ESP32 retained UI/installed worker/显式 typography fixture、P4/P5/P6 smoke、ScriptTaskFrameRenderer/Canvas/benchmark | Win32 不再将 GDI 作为文本度量或绘制后端；设备产品和 worker 只注册 Core 字体资源。决定并落实 legacy acceptance fixtures 的去留，不让旧回调继续成为必需构建路径。 |
| 5. API / consumer sweep | Core public headers、Core tests/benchmarks/package consumers、Runtime installed-package consumers 与 porting docs | 删除 `TextMeasureProvider`、`TextPainter`、range/family/additive hooks 和实现分支；改写 callback 专用单测及 `c5_flex_grid_consumer`、`wrapped_row_flex_consumer`，复核 full/cropped、禁用能力和 installed package。 |

已核实的非重复工作：AppFontSet 已 JFFONT2-only；Canvas2D、embedded host 已使用 Core TextSystem；installed worker 有受限的 painterless v10 路径，但它拒绝一般 legacy Text 命令，不能算作完整迁移。WS147 `mainline1` 已完成实机验收，不重测；它不替代 `mainline2` 或 worker/BMP 命名 profile 的验收。

测试迁移重点：Core 的 `shared_inline_tests.cpp::generated_text_fallback_and_invalidation` 已覆盖 #65 生成文本使用 TextSystem 或拒绝的行为；`text_system_tests.cpp::core_prepared_text_ellipsis` 覆盖 ellipsis owner 冲突时整体拒绝。纯旧接口覆盖主要在 `text_provider_tests.cpp`，应改为 TextSystem 换行/度量行为测试或删除。Runtime 的 `canonical-abi4-installed-consumers` full/cropped 是安装消费者主门槛，但需要将精确 Core #65 source identity 纳入；还要迁移 `c5_flex_grid_consumer` 与 `wrapped_row_flex_consumer`。保留并运行 R3 full/cropped、Node identity on/off、无 RTTI 矩阵。16 组 feature profile 当前是 configure/manifest 检查，不应误报为 16 组完整编译。
