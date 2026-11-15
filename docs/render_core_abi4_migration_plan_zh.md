# Render Core ABI4 集成计划

> 最后更新：2026-11-15；适用版本：0.6.0-dev。Core 开发契约：0.9.0-dev / ABI4。

> **最新检查点（2026-11-15）：**Runtime PR #99–#110 已合并，最新 merge commit `1d9071a0`。PR #110 通过 21/21 项 CI：把 P4/P5/P6 CoreText smoke 放到 benchmark framebuffer 准入之前，覆盖 ASCII/CJK 与 400/500/700 字重；并修正 Win32 CoreText 文本变化时误用旧 provider 度量判定布局复用的问题，当前保守重建布局。IDF 5.3.1、Core identity `e842b2e62fd51777cbc6dd6a6a9fea3b9ea68938ce4a6e434eb7c2baaf1adb67`（163 inputs）构建通过，CoreText=y，map 中没有旧 Noto C++ 字体符号，smoke BIN 3,503,456 B（SHA-256 `150a148ce548e97b95f7b7a7c95c7ec4f07a5b51e7f9fb715cdc218aa8c22aae`）。这是构建证据，尚未刷板运行。PR #107（`9ebaf806`）及专用 Core #63 v10 worker full/cropped lane 覆盖 popup 输入、prepared packet、可见行像素、选项选择和弹层关闭；PR #109（merge `725184c3`）增加 WS147 installed-worker 验收配置和 select-popup app，并修正 provider 在发送 `INSTALL_BEGIN` 前准备本地取消会话。Core PR #63（merge `8aceb6d8`，source commit `89acc36a`，source identity `e842b2e62fd51777cbc6dd6a6a9fea3b9ea68938ce4a6e434eb7c2baaf1adb67`，163 个实现输入）让开放 select popup 行使用 Core `PreparedText`。PR #103（`d6fcaff4`）将应用字体统一为 JFFONT2 并移除旧 AppFontSet/AppHost 回调路径；PR #105（`81d99750`）在 CoreText 构建中排除旧 Noto C++ 字体包。当前未合并 Runtime 工作树还把 WS147 band-shell profile 默认切到 CoreText；profile 回归和 Core #63 / ESP-IDF 5.3.1 构建通过，BIN 3,524,112 B（SHA-256 `38865D6AB9FCA8D57ABE3766BA8029E4DCF68516B87FC6DBEB5998790DFA751E`），8 MiB app 分区约余 58%，map 无旧 Noto C++ 字体符号。这只是构建证据，不是新实机验收；已完成的 `mainline1` 观察不重做。Win32 CoreText start/end、left/right 对齐和属性变化的增量/全量重绘已对精确 Core #63 ABI4 验证通过（293 帧、27 组流程），矩阵尚未合入。ABI4 尚未签名发布，Runtime 仍锁定签名 Core 0.8.0 / ABI3；`mainline2` 与 installed-worker popup 仍为独立镜像。

本文件是当前执行计划。旧检查点保存在 Git 历史和对应 PR 中，不再作为额外待办。
[文本消费者退役清单](render_core_text_consumer_retirement.md)记录实际调用方与原生设备候选的完整身份。

## 当前检查点

| 边界 | 当前状态 | 剩余门槛 |
| --- | --- | --- |
| 已发布依赖 | Runtime 生产锁仍为签名 Core v0.8.0 / ABI3。 | 最终 ABI4 契约发布后，更新精确依赖锁。 |
| Core 主线 | PR #63（`8aceb6d8`）含 source commit `89acc36a`，CI 已通过；0.9.0-dev / ABI4，source identity `e842b2e62fd51777cbc6dd6a6a9fea3b9ea68938ce4a6e434eb7c2baaf1adb67`，163 个实现输入。开放 select popup 行现使用 Core `PreparedText`。 | Runtime 消费者资格、generated-content 与回调退役。 |
| Runtime 主线 | PR #110，merge `1d9071a0`，CI 21/21；#99–#110 均已合并，新增 CoreText benchmark smoke 覆盖和 Win32 CoreText 文本变更重用回归。 | 审查并合入当前 ESP32 CoreText 默认 profile 与 Win32 ABI4 属性/重绘矩阵；generated-content 回调退役和 ABI4 发布门槛仍未完成。 |
| Embedded host | #100 为 layout/layers/paint 绑定同一个 TextSystem/JFFONT2；锁定 ABI3 和 source ABI4 full/cropped 测试通过。 | 该示例不再依赖文本 provider 回调。 |
| Installed worker | 已合并 #101，让无 painter 的 canonical v10/CoreText 独立于 BMP 工作。#107 用 canonical v10 worker、精确 Core #63 源码和 prepared packet 覆盖 popup 输入、行像素、选择与关闭；#109 增加 full-feature WS147 profile 与 select-popup app，并通过 IDF 构建、打包及 provider 回归。 | 独立 popup profile 尚缺人工画面/触控观察；还需迁移剩余依赖回调的 generated/widget 文本。 |
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
| ESP32 retained native/default | Runtime commit `66be335c` 在 CoreText 构建中排除 generated C++ 字体包。当前未合并工作树已把 WS147 band-shell profile 默认设为 CoreText；profile 回归和精确 Core #63 ESP-IDF 构建通过，legacy typography acceptance 仍显式隔离。 | 审查/合入 profile 改动；legacy typography acceptance 保持隔离，页面路由/支持属性另行验收。 |
| ESP32 P4/P5/P6 smoke | `main.cpp` 中独立 provider 仅被 benchmark/smoke fixture 使用；CoreText profile 现在已有单独 CoreText smoke 构建证据。 | 保留旧 fixture 为显式 legacy-only，并让 CoreText smoke 覆盖同等 ASCII/CJK/字重和控件场景；它不是产品默认消费者。 |
| Win32 默认路径 | 显式 CoreText 目录模式已合并，默认仍为 GDI。#110 修正 CoreText 文本变化不能使用 legacy provider 校验布局复用，并通过脚本回归。当前未合并 ABI4 矩阵覆盖 start/end、left/right 对齐和属性变化的增量/全量重绘，293 帧、27 组流程通过。 | 审查/合入矩阵，关闭剩余已支持属性缺口，再决定是否采用 CoreText 默认目录。 |
| AppFontSet | #103 已完成：目录仅接受 JFFONT2，移除回调访问器/浏览器优先级，并转换样例和工具。 | AppFontSet 无剩余工作；generated C++ BitmapFont 默认资源另行跟踪。 |

Canvas2D 已使用 TextSystem，不是剩余回调消费者。Installed worker 候选在写帧缓冲前
拒绝依赖回调的普通 `Text` 命令；这是有界准入规则，不代表旧路径支持的全部
生成内容或控件文字均已迁移。

已合并 #101 的 worker 主机证据：Core PR60 full 和关闭四类可选能力的 cropped 均通过 7/7；
BMP OFF/ON 都覆盖可见 prepared text、JavaScript 文字修改、退出后的重放和原子拒绝。
锁定的已安装 ABI3 包通过四项 admission 测试，两项 canonical 测试按预期跳过。
CoreText OFF 的 legacy 路径保持显式选择。

### C. Core 文本 API 收敛

Core #61 已在四项 CI 全部通过（run `43670844631`）后合并为 `3152300a`。有界 owner-side ellipsis 的 MSVC full 测试 33/33、已安装 full/cropped 文本消费者各 1/1 通过；cropped 唯一 shared-inline 阴影断言失败在未修改基线同样复现。Runtime #102 已合入该精确源码 pin、五帧真实字体增量/全量省略号测试和 clip-only 可见对照。Core #62 为闭合原生控件 label/select arrow 提供 Core 度量与事务式 prepared packet，已合并为 `38148e73`。Core #63（`89acc36a`）在发布 popup overlay 前准备所有可见选项；缺字或预算拒绝时整个 overlay 都会拒绝。本 Runtime 分支用触控、prepared packet、B 行像素和选中/关闭回归验证该消费者。generated-content 所有权和更广属性/默认采用仍独立推进。另有 generated-content/list-marker 在 shared-inline 不适用或准入失败时仍可能发出旧 `push_text()`；退役旧回调前，须改为 Core TextSystem 准备路径或明确拒绝，并覆盖两种结果。实现必须保留源文本布局/固有尺寸，在最终几何
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

1. 已合并：Runtime #99–#110。Runtime #110 通过 21/21 CI；完成 P4/P5/P6 CoreText smoke 构建验证，并修复 Win32 CoreText 文本变化的布局重用资格。
2. 审查并合入当前 ESP32 CoreText 默认 profile 与 Win32 ABI4 属性/重绘矩阵。Band-shell CoreText 构建与 profile guard 通过；Win32 矩阵 293 帧、27 组流程通过。
3. 闭合 Core generated-content/list-marker 所有权：将旧 `push_text()` fallback 改为 Core TextSystem 准备或明确拒绝，并覆盖两种结果；legacy typography acceptance 保持独立。
4. 决定 Win32 GDI 默认是否迁移；转换剩余依赖回调的测试/fixture，再在协调预发布变更中删除公开 provider/range/family/additive API 及实现分支。
5. 消费者、full/cropped、禁用特性与源码归档门槛通过后，签名发布 ABI4 并更新 Runtime 精确生产依赖锁。
6. 硬件可用时分别跟踪 `mainline2` 与 installed-worker popup 专用镜像；`mainline1` 观察已记录，不重复测试，也不能替代主机/API 发布门槛。
