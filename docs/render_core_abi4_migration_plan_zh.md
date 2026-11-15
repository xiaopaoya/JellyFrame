# Render Core ABI4 集成计划

> 最后更新：2026-11-16；适用版本：0.6.0-dev。Core 开发契约：0.9.0-dev / ABI4。

> **最新检查点（2026-11-16）：**Runtime PR #111（`fa5176a0`）和 Core PR #65（`1d5c746d`，source `d327eee4`）已合并；Runtime #99–#111 均已合并。隔离 Core ABI4 回调退役候选源码 identity 为 `8328dbf1...02d1186` / 159 个文件，full/cropped Release CTest 分别 33/33、32/32。恢复不依赖回调的纯样式布局复用后，Runtime 精确 source override 的 Release CTest 为 113/113；route-tabs 重建数为 1，motion-lab 报告稳定布局复用。Core 源码归档在干净快照中 3/3 通过；安装消费者 full/cropped TextSystem 各 1/1、C5 1/1、wrapped-row 1/1、natural-image 1/1。Core #63 popup 的 `installed-core63-device1` 实机验收通过（Runtime `f609a1d4`、Core source `e842b2e6`、BIN SHA-256 `8DBA1F73...E2A6A0F`）：选项均可见，选择 CHARLIE 后结果更新正确。测试后已恢复此前通过验收的 `mainline1`（BIN SHA-256 `AFB9F47E...46C696E`，写入校验通过）。Runtime 生产仍锁定签名 Core 0.8.0 / ABI3；ABI4 尚未签名发布。

本文件是当前执行计划。旧检查点保存在 Git 历史和对应 PR 中，不再作为额外待办。
[文本消费者退役清单](render_core_text_consumer_retirement.md)记录实际调用方与原生设备候选的完整身份。

## 当前检查点

| 边界 | 当前状态 | 剩余门槛 |
| --- | --- | --- |
| 已发布依赖 | Runtime 生产锁仍为签名 Core v0.8.0 / ABI3。 | 最终 ABI4 契约发布后，更新精确依赖锁。 |
| Core 主线 | PR #65（merge `1d5c746d`，source `d327eee4`）在 #63 后补齐 generated pseudo label/list marker 的 Core TextSystem 准备或明确拒绝；四项 CI 全通过。回调退役已在 Core PR #66 提交，隔离源码候选与消费者门禁见下文。 | 等待 Core PR #66 审查与 CI，通过后合并，再锁定其精确发布源码。 |
| Runtime 主线 | PR #111，merge `fa5176a0`；#99–#111 均已合并。Runtime PR #114 退役回调消费者、将 Win32 默认路径迁至 Core TextSystem，并保留只比较 Core 计算样式的无回调布局复用；精确 source override 的 Release CTest 113/113 通过。 | 等待 Runtime PR #114 审查与 CI；Core #66 发布后更新签名 ABI4 生产依赖锁。 |
| Embedded host | #100 为 layout/layers/paint 绑定同一个 TextSystem/JFFONT2；锁定 ABI3 和 source ABI4 full/cropped 测试通过。 | 该示例不再依赖文本 provider 回调。 |
| Installed worker | 已合并 #101，canonical v10/CoreText 无 painter 且独立于 BMP。#107 覆盖 popup 输入、prepared packet、行像素、选择和关闭；专用 `installed-core63-device1` 全功能 WS147 镜像的用户可见弹层验收现已通过。 | 该镜像无需重复验收；后续不同 worker/profile 镜像须绑定各自身份测试。 |
| 原生 WS147 | `mainline1` 的首页/Activity/Weather 验收仍有效；popup 测试结束后板子已恢复到该镜像。独立的 `mainline2` 尚未刷写。 | 仅在选定 `mainline2` 做资格验证时测试该精确镜像，无需重测 `mainline1`。 |
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

ABI4 **尚未发布**。从最终审查通过的 API/资源契约生成确定性签名发布，核验 tag、
归档 checksum、source manifest、profile 和独立安装消费者，再更新 Runtime 的精确
版本、ABI、源码/归档锁、已发布产物 CI 和兼容性文档。ABI3 作为回退证据保留，
不能声称 ABI3/ABI4 二进制互换。

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
Installed worker/BMP 必须使用独立命名镜像和资源配置。

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
4. 已在源码 identity `8328dbf1...02d1186` / 159 个文件上完成：Core full/cropped CTest、归档 3/3、full/cropped TextSystem 安装消费者、C5、wrapped-row、natural-image 消费者，以及 Runtime CTest 113/113。
5. Core PR #66 与 Runtime PR #114 均已开放。审查协调后的 API diff、包清单和源码身份，等待两边 CI 通过后再合并。
6. 审查与 CI 通过后签名发布 ABI4，并将 Runtime 精确生产依赖锁更新到该发布版本。
7. 保持 `mainline1` 验收关闭。仅当选择资格验证时，才对 `mainline2` 或后续 worker 镜像单独测试。
