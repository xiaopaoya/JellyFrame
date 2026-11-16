# Render Core ABI4 集成计划

> 最后更新：2026-11-16；适用版本：0.6.0-dev。Core 已发布版本：0.9.0 / ABI4。

> **最新检查点（2026-11-16）：**Runtime PR #99–#117 已合并；PR #120 已在 23/23 检查通过后合并为 `7dd34aea69e6163541132fe40dc130ea747be304`，完成 provider manifest/profile 与固件 provenance 校验。PR #116（`f2169d652e4e4c16a84935cd955f4524ace14db0`）将生产依赖锁定到签名发布的 Render Core v0.9.0 / ABI4；PR #117（`673f23a2778894287e292473980903fbe8e7cff3`）修复 ABI4 WS147 移植构建边界，当前 Runtime master 为 `7dd34aea`。Core v0.9.0 / ABI4 已签名发布，归档及 sidecar digest 已核验。PR #116/#117 CI 均通过。

PR #117 合并树上的 WS147 ABI4 native 与 device-image lifecycle acceptance 镜像均已构建。native BIN SHA-256 为 `A87BBFDC95F6467A065A0A5A5B436E7CD1730D22C838BEE9D44590E40D148039`，ELF SHA-256 前缀为 `68642F8BE62C6060`；串口启动日志确认首帧呈现成功（`first_present_ok=1`，580.297 ms）。用户已验收该 native 镜像：Home 文字/卡片、Activity 标题/正文及返回、Weather 大卡片与四个小卡片标签均正常，并确认一个可见按钮触控响应。另一镜像 `0.6.2-abi4-pr116-inst1` 已刷入并启动，BIN SHA-256 为 `511C6A48AD1BCA5A80D1653CA71435CACABD92E2362A22EAF542C08F660252EB`，ELF 前缀 `2F34A22D`；用户观察到黑屏且背光未亮。复核该镜像 sdkconfig 后确认它启用的是 `RUN_DEVICE_IMAGE_LIFECYCLE_ACCEPTANCE`、关闭 band-shell UI，此前首次刷写时尚未经 JFDP 安装并启动应用，因此该固件待命时没有可见 UI 是预期行为。这不是 worker/CoreText 渲染验收，也不能据此判定显示故障。当时 native1 已重新刷回并通过 esptool 校验，用户确认正常；随后本轮已重新刷入 inst1 并安装启动 generation200 应用，当前板上为 installed 应用。构建使用的共享 ESP-IDF checkout 标记为 v5.3.1-dirty（97 个修改路径）；清单注明隔离源码文件匹配固定的 IDF tree。

本文件是当前执行计划。旧检查点保存在 Git 历史和对应 PR 中，不再作为额外待办。
[文本消费者退役清单](render_core_text_consumer_retirement.md)记录实际调用方与原生设备候选的完整身份。

## 当前检查点

| 边界 | 当前状态 | 剩余门槛 |
| --- | --- | --- |
| 已发布依赖 | Core v0.9.0 / ABI4 已签名发布；两个资产均上传，远端归档 digest 与本地一致。 | 无。 |
| Core 主线 | Core PR #67（`16a83211ec0992f8e25ec0fc977eb6fd158716e3`）及 ABI4 发布门槛已完成。 | 无。 |
| Runtime 主线 | #116 生产锁、#117 WS147 编译修复、#120 provider provenance 校验均已合并；master `7dd34aea`，#120 检查 23/23。 | 集成门槛完成；当前 generation200 应用画面及触控人工验收已通过。 |
| Embedded host | #100 为 layout/layers/paint 绑定同一个 TextSystem/JFFONT2；锁定 ABI3 和 source ABI4 full/cropped 测试通过。 | 该示例不再依赖文本 provider 回调。 |
| Installed worker | 精确 inst1 BIN 已在 COM19 重刷并通过 esptool hash 校验；JFDP 发现/info 确认 Core0.9.0/ABI4 和源码 d4ba0b84，generation200 安装 accepted、启动 ok，日志 render_status=0、accepted_seq=1、presents_failed=0。 | generation200 人工画面及触控确认于 2026-11-16 通过，覆盖本次三资源应用。 |
| 原生 WS147 | #117 后的 ABI4 native 镜像已构建、刷写、串口启动并通过指定画面/触控人工验收；BIN SHA-256 `A87BBFDC95F6467A065A0A5A5B436E7CD1730D22C838BEE9D44590E40D148039`，ELF SHA-256 前缀 `68642F8BE62C6060`。 | 无；验收仅覆盖该精确镜像及记录的观察项。 |
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
这两个 BIN 使用不同 sdkconfig，不能直接把 BIN 差值视作字体包净开销。后续：迁移/退役其他 generated-font 消费者，并保留确定的 family/weight 选择、
字体字节寿命、缺字拒绝和资源预算；不增加新的宿主文本回调。

### B. Runtime 消费者迁移

| 消费者 | 进展 | 完成证据 |
| --- | --- | --- |
| Embedded host | #100 完成。 | 可见文字、click/checkbox/select、RGB565 flush；ABI3 与 ABI4 full/cropped。 |
| ESP32 installed worker | 精确 inst1 BIN 已在 COM19 重刷并通过 esptool hash 校验；JFDP 发现/info 确认 Core0.9.0/ABI4 和源码 d4ba0b84，generation200 安装 accepted、启动 ok，日志 render_status=0、accepted_seq=1、presents_failed=0。 | generation200 人工画面及触控确认于 2026-11-16 通过，覆盖本次三资源应用。 |
| ESP32 retained native/default | Runtime #114 将生产文本消费者迁至 Core-owned TextSystem；ABI3 兼容代码仍由 ABI 条件隔离。 | PR #117 后的锁定 ABI4 native 镜像已完成构建、刷写、启动和所记录的 Home/Activity/Weather/按钮触控验收。 |
| ESP32 P4/P5/P6 smoke | `main.cpp` 中独立 provider 仅被 benchmark/smoke fixture 使用；CoreText profile 现在已有单独 CoreText smoke 构建证据。 | 保留旧 fixture 为显式 legacy-only，并让 CoreText smoke 覆盖同等 ASCII/CJK/字重和控件场景；它不是产品默认消费者。 |
| Win32 默认路径 | #114 已将默认路径切换至 Core TextSystem，并保留经 Core 比较计算样式后的无回调布局复用；GDI 兼容实现只用于 ABI3。#111 的属性/重绘矩阵覆盖 293 帧、27 组流程，#116 的已发布包 CI 与 ABI3 兼容 lane 均通过。 | 无 ABI4 回调迁移或发布矩阵门槛。 |
| AppFontSet | #103 已完成：目录仅接受 JFFONT2，移除回调访问器/浏览器优先级，并转换样例和工具。 | AppFontSet 无剩余工作；generated C++ BitmapFont 默认资源另行跟踪。 |

Canvas2D 已使用 TextSystem。Runtime #114 移除了剩余生产 host measure/paint 消费者，
并针对 generated pseudo-content 路径验证由 Core 准备文本或明确拒绝。Core #66 已退役
旧回调 API；合并后的 Runtime/Core 均已通过 PR 和 CI。生产回调消费者迁移、包交付和 ABI4 发布资格均已完成，
native 指定观察项已验收，当前设备门槛是 generation200 installed-worker 人工画面与触控确认。

已合并 #101 的 worker 主机证据：Core PR60 full 和关闭四类可选能力的 cropped 均通过 7/7；
BMP OFF/ON 都覆盖可见 prepared text、JavaScript 文字修改、退出后的重放和原子拒绝。
锁定的已安装 ABI3 包通过四项 admission 测试，两项 canonical 测试按预期跳过。
CoreText OFF 的 legacy 路径保持显式选择。

### C. Core 文本 API 收敛

Core #61 已在四项 CI 全部通过（run `43670844631`）后合并为 `3152300a`。有界 owner-side ellipsis 的 MSVC full 测试 33/33、已安装 full/cropped 文本消费者各 1/1 通过；cropped 唯一 shared-inline 阴影断言失败在未修改基线同样复现。Runtime #102 已合入该精确源码 pin、五帧真实字体增量/全量省略号测试和 clip-only 可见对照。Core #62 为闭合原生控件 label/select arrow 提供 Core 度量与事务式 prepared packet，已合并为 `38148e73`。Core #63 准备 popup 的全部可见选项；Core #65 将 generated pseudo label/list marker fallback 迁移到 Core TextSystem 准备或明确拒绝。Core #66 已合并并退役文本度量/绘制回调；Runtime #114 已迁移对应生产消费者，相关 CI 通过。Core v0.9.0 / ABI4 已签名发布；Runtime #116 已通过支持的工具/基准构建覆盖、源码归档、已发布 full/cropped 安装消费者和关闭能力矩阵，并更新精确生产锁。实现必须保留源文本布局/固有尺寸，在最终几何
确定后按 owner 裁剪，使用 Q1000/spacing 和输出预算，并对不支持的完整上下文拒绝，
不回退宿主。混合 owner 需要整段截断所有权，不能各叶节点独立截断。

Core #66 已完成旧 provider API 和对应 Core 回调分支的集中退役。仅为明确隔离的 ABI3
生产兼容路径仍允许使用原 ABI3 包；测试和移植文档中的历史引用应标记为 ABI3 或历史说明，
不应重新引入 ABI4 别名。

完成门槛：生产路径不存在回调消费者。合并源码上的 full/cropped 消费者、Runtime 已发布包矩阵、ABI3 兼容 lane 和 Core CI 均已通过；ABI4 已签名发布并由 Runtime #116 锁定。

### D. 发布和生产锁

Core v0.9.0 / ABI4 已从最终审查通过的源码发布并签名；tag、归档 checksum、source
manifest、profile 和 Release 资产均已核验。Runtime 精确版本、ABI、源码/归档锁及
已发布产物 CI 已随 Runtime #116 合并并通过。ABI3 作为回退证据保留，不能
声称 ABI3/ABI4 二进制互换。

### E. 设备准入

PR #117 后的生产锁定构建由 `D:/CodexTemp/Runtime-pr116-ws147-build-20261116/build-manifest.json`
记录，Runtime build commit `d4ba0b846ca21a7d48d420651493133d8b1c209a`，tree
`2e0326369275400b03c75ee20740b91164fc2ea6`（与 master `673f23a2` 的树一致），Core
v0.9.0 / ABI4。native profile 为 `0.6.2-abi4-pr116-native1`，入口 `/band_shell.html`、
CoreText ON、installed scripting OFF；BIN SHA-256
`A87BBFDC95F6467A065A0A5A5B436E7CD1730D22C838BEE9D44590E40D148039`，ELF SHA-256
`68642F8BE62C606092F76DE1FDDE04D0E69D659DCC425C2A56DBAD27751B4F92`。app 占用
3,522,112 字节，8 MiB 分区余量 4,866,496 字节。串口启动日志报告 `first_present_ok=1`
（580.297 ms）。用户确认 Home 文字/卡片、Activity 标题和正文及返回、Weather 大卡片和四个小卡片标签正常，并确认一个可见按钮有触控响应；该观察绑定到此精确 BIN 身份。

该构建清单的 `deviceAcceptance.flashed=false` 与 `physicalDeviceTested=false` 记录的是构建时状态；后续刷写/启动观察见下方。它们不代表已验收已安装应用的渲染。IDF v5.3.1 checkout
被报告为 dirty（共享 checkout 有 97 个修改路径），但隔离源码文件与固定 IDF tree 匹配。

历史设备测试 `mainline1` 身份为 Runtime `75a435c5`、Core `34ae4b88`，BIN SHA-256
`AFB9F47E015FF5EB06AD441EBC322F50B04D6149CFA3E70B8C96675EF46C696E`，ELF SHA-256
`B462F78731662B53A21DEAEE1F86E4E0995F37D2F964BEDDA3D27B284FA522910`。启动日志确认
`/band_shell.html`、CoreText 240 MHz、触控检测和首帧呈现成功。用户确认首页文字完整清晰，
Activity 与 Weather 文字显示正常，Weather 下方四个小卡片均有文字，页面返回正常。该结果只
关闭此历史 native 镜像上的观察项，不覆盖本次 PR #117 后的 native 镜像、installed worker/BMP
或 P4/P5/P6 benchmark smoke。此前的小卡片缺字反馈仍适用于其他未测身份。历史 `mainline2`
与本次生产锁构建身份不同，不能代替当前镜像验收。

### 当前 installed 应用检查点（2026-11-16）

当前 COM19 为 `0.6.2-abi4-pr116-inst1`，BIN SHA-256 `511c6a48ad1bca5a80d1653ca71435cacabd92e2362a22eaf542c08f660252eb`，已通过 esptool 刷写 hash 校验。typed JFDP info 确认 Core0.9.0/ABI4，Runtime source `d4ba0b846ca21a7d48d420651493133d8b1c209a`。应用 `org.jellyframe.fixture.installed-script-input` bundle 为2420字节，SHA-256 `949f9904ef9dadc38926b6c8d433d4340e6218de9f908a0b8a70103370f264bc`。registryGeneration200 安装 accepted、启动 ok；日志 `render_status=0`、`accepted_seq=1`、`presents_failed=0`。2026-11-16 人工确认 Installed script / Ready: 0 / Tap 初始画面、点击计数和按钮颜色变化均正常，本次画面及触控验收通过。

本 fixture 有三个资源，没有 bitmap 图片。首次串口采集为空，当前身份与运行证据来自 typed JFDP info/logs；资源遥测为采样结果。旧待命黑屏发生在应用安装之前。构建清单中的 flashed=false/physicalDeviceTested=false 为构建时状态，后续刷写与本节运行记录按时间保留。PR #120 的 provider 发布/校验门槛已完成，当前 generation200 人工画面和触控观察已通过；bitmap、弹层及更广泛生命周期/资源场景保留各自验证范围。

## 依赖顺序

```text
合并后的 Core/Runtime ABI4 源码 -> 签名发布归档 -> Runtime 精确依赖锁
                                                         |
                                                命名设备构建
                                                         |
                                       native / installed-worker 验收
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
7. 已完成：Runtime #116 锁定 Core v0.9.0 / ABI4 并通过发布包、ABI3 兼容和 Windows CI；Runtime #117 修复 ABI4 WS147 编译边界并合并至 master `673f23a2`。
8. 已完成构建和 native 指定观察项验收；inst1 已重刷校验，generation200 JFDP 安装 accepted、启动 ok，渲染遥测成功。generation200 的画面和触控已于 2026-11-16 获人工确认通过。旧 popup 与 mainline1 观察保留各自镜像范围。
