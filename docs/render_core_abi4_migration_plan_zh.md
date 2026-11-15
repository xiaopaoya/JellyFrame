# Render Core ABI4 集成计划

> 最后更新：2026-11-16；适用版本：0.6.0-dev。Core 已发布版本：0.9.0 / ABI4。

> **最新检查点（2026-11-16）：**Runtime PR #99–#115 已合并。#111 将 WS147 band-shell CoreText profile 设为默认并加入 Win32 ABI4 属性/增量与全量重绘矩阵（293 帧、27 组流程）；#114 退役 Runtime 文本回调，#115 扩展 ABI4 桌面工具和 benchmark 构建覆盖。Core PR #67（`16a83211ec0992f8e25ec0fc977eb6fd158716e3`）完成 ABI4 契约。Core v0.9.0 / ABI4 已签名发布；归档与 `.sha256` sidecar 已上传，GitHub 归档 digest 与 `8a0568b548a7e30956c515bba753ad7fe7f0fbb501e319cbef926eec2b855628` 一致。Runtime 候选 `codex/core-0.9.0-abi4-lock-20261116` 更新锁、manifest/模板、provenance 检查和 full/cropped 安装消费者，本地两种 profile 的 CTest 各 22/22；另修正三个仍生成 Core 0.7.0 的 ESP32 探针。PR #116 已开启；master 经 #115 前进后产生合并冲突，更新尚未完成，CI 也未启动。Core #63 弹层在命名 profile `0.6.2-abi4-installed-core63-device1` 上通过实机验收。Runtime master 在 #116 CI 通过并合并前仍锁定 Core 0.8.0 / ABI3。`mainline2` 是独立设备工作。

本文件是当前执行计划。旧检查点保存在 Git 历史和对应 PR 中，不再作为额外待办。
[文本消费者退役清单](render_core_text_consumer_retirement.md)记录实际调用方与原生设备候选的完整身份。

## 当前检查点

| 边界 | 当前状态 | 剩余门槛 |
| --- | --- | --- |
| 已发布依赖 | Core v0.9.0 / ABI4 已签名发布；两个资产均上传，远端归档 digest 与本地一致。 | Runtime PR #116 通过 full/cropped、ABI3 source-override 和其余 CI 后合并。 |
| Core 主线 | Core PR #67（`16a83211ec0992f8e25ec0fc977eb6fd158716e3`）及 ABI4 发布门槛已完成。 | 无；Runtime 由 #116 消费发布包。 |
| Runtime 主线 | PR #99–#115 已合并。候选 #116 更新精确锁、app manifest/模板、provenance CI、ABI4 安装消费者和三个 ESP32 探针；本地 full/cropped CTest 各 22/22。 | 更新候选以解决最新 master 冲突并启动 PR CI；所有必需检查通过后合并。 |
| Embedded host | #100 为 layout/layers/paint 绑定同一个 TextSystem/JFFONT2；锁定 ABI3 和 source ABI4 full/cropped 测试通过。 | 该示例不再依赖文本 provider 回调。 |
| Installed worker | #101/#107/#109/#114 已合并；用户已验收 Core #63 popup 的标题、全部选项、选择结果和触控。 | #116 合并后，从生产锁构建独立 worker 镜像并绑定身份验收。 |
| 原生 WS147 | `mainline1` 的首页/Activity/Weather 验收仍有效；弹层测试后已恢复该镜像。 | 对签名 ABI4 锁定后的新原生镜像做身份绑定验收；不重复测试旧 `mainline1`。 |
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
| ESP32 retained native/default | Runtime #114 已将生产文本消费者迁至 Core-owned TextSystem；ABI3 兼容代码仍由 ABI 条件隔离。旧 `mainline1` 镜像早于 #114/#66。 | 精确 ABI4 包锁定后构建并验收新的命名镜像。 |
| ESP32 P4/P5/P6 smoke | `main.cpp` 中独立 provider 仅被 benchmark/smoke fixture 使用；CoreText profile 现在已有单独 CoreText smoke 构建证据。 | 保留旧 fixture 为显式 legacy-only，并让 CoreText smoke 覆盖同等 ASCII/CJK/字重和控件场景；它不是产品默认消费者。 |
| Win32 默认路径 | #114 已将默认路径切换至 Core TextSystem，并保留经 Core 比较计算样式后的无回调布局复用；GDI 兼容实现只用于 ABI3。#111 的属性/重绘矩阵覆盖 293 帧、27 组流程。 | 用精确 ABI4 发布包运行发布矩阵；ABI4 下不再有 GDI 回调迁移工作。 |
| AppFontSet | #103 已完成：目录仅接受 JFFONT2，移除回调访问器/浏览器优先级，并转换样例和工具。 | AppFontSet 无剩余工作；generated C++ BitmapFont 默认资源另行跟踪。 |

Canvas2D 已使用 TextSystem。Runtime #114 移除了剩余生产 host measure/paint 消费者，
并针对 generated pseudo-content 路径验证由 Core 准备文本或明确拒绝。Core #66 已退役
旧回调 API；合并后的 Runtime/Core 均已通过 PR 和主线 CI。剩余门槛是包交付与发布资格，
不再是生产回调消费者迁移。

已合并 #101 的 worker 主机证据：Core PR60 full 和关闭四类可选能力的 cropped 均通过 7/7；
BMP OFF/ON 都覆盖可见 prepared text、JavaScript 文字修改、退出后的重放和原子拒绝。
锁定的已安装 ABI3 包通过四项 admission 测试，两项 canonical 测试按预期跳过。
CoreText OFF 的 legacy 路径保持显式选择。

### C. Core 文本 API 收敛

Core #61 已在四项 CI 全部通过（run `43670844631`）后合并为 `3152300a`。有界 owner-side ellipsis 的 MSVC full 测试 33/33、已安装 full/cropped 文本消费者各 1/1 通过；cropped 唯一 shared-inline 阴影断言失败在未修改基线同样复现。Runtime #102 已合入该精确源码 pin、五帧真实字体增量/全量省略号测试和 clip-only 可见对照。Core #62 为闭合原生控件 label/select arrow 提供 Core 度量与事务式 prepared packet，已合并为 `38148e73`。Core #63 准备 popup 的全部可见选项；Core #65 将 generated pseudo label/list marker fallback 迁移到 Core TextSystem 准备或明确拒绝。Core #66 已合并并退役文本度量/绘制回调；Runtime #114 已迁移对应生产消费者，两个合并后 CI 均通过。现在工作进入 ABI4 包发布资格阶段：仍须完成支持的工具/基准构建覆盖、源码归档、已安装 full/cropped 消费者与关闭能力矩阵，再签名发布。实现必须保留源文本布局/固有尺寸，在最终几何
确定后按 owner 裁剪，使用 Q1000/spacing 和输出预算，并对不支持的完整上下文拒绝，
不回退宿主。混合 owner 需要整段截断所有权，不能各叶节点独立截断。

Core #66 已完成旧 provider API 和对应 Core 回调分支的集中退役。仅为明确隔离的 ABI3
生产兼容路径仍允许使用原 ABI3 包；测试和移植文档中的历史引用应标记为 ABI3 或历史说明，
不应重新引入 ABI4 别名。

完成门槛：生产路径不存在回调消费者。合并源码上的 full/cropped 消费者和 Core CI 已通过，
ABI4 已签名发布；Runtime 下载包矩阵和 ABI3 兼容 lane 是 PR #116 的剩余集成门槛。

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
benchmark smoke。此前的小卡片缺字反馈仍适用于其他未测身份。旧 `mainline2` 是合并前产物，
不是 ABI4 发布资格目标；生产锁更新后需为 native 与 installed worker 分别构建新命名镜像。

## 依赖顺序

```text
合并后的 Core/Runtime ABI4 源码 -> 签名发布归档
                                           |
                                  Runtime 精确依赖锁
                                           |
                            命名设备构建与实机验收
```

主机实现/审查、安装包验证和设备构建准备可以并行。默认采用遵循属性和资源门槛。
可选 ABI5 reuse 与生产 shaping 继续作为独立工作。

## 紧接着的工作

1. 已完成：Runtime #111 合入 WS147 CoreText 默认 profile，以及 Win32 293 帧、27 组流程的属性与增量/全量重绘矩阵。
2. 已由 Core #65 完成：generated pseudo label/list marker 使用 Core TextSystem 准备或明确拒绝；Core 四项 CI 全部通过。
3. 已完成：Runtime 使用精确 Core #65 identity `52ae4354...7302f62d` 完成 Release build 与 CTest 60/60；source override 测试配置也已链接 Core test hook。这证明 host 集成，不代表签名包锁定或设备验收。
4. 已在源码 identity `8328dbf1...02d1186` / 159 个文件上完成：Core full/cropped CTest、归档 3/3、full/cropped TextSystem 安装消费者、C5、wrapped-row、natural-image 消费者，以及 Runtime CTest 113/113。
5. 已完成：Core #66/#67 与 Runtime #114/#115 合并，相关合并后 CI 均通过。
6. 已完成：Core v0.9.0 / ABI4 已签名发布；归档和 sidecar digest 已核验。Runtime full/cropped 本地 CTest 各 22/22。
7. 当前：更新 Runtime PR #116 以解决 master 新提交带来的冲突，并确认 PR CI 启动；仅在 full/cropped、ABI3 source-override 和其他必需 checks 全部通过后合并。
8. 合并锁更新后，从生产锁构建命名 WS147 native 与 installed-worker 镜像，记录 BIN/ELF/包身份并按具体 profile 验收。已通过的 popup 观察和旧 `mainline1` 观察只适用于其命名镜像。
