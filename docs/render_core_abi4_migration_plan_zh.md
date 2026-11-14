# Render Core ABI4 集成计划

> **IRAM 状态（2026-11-14）：已确认是报告口径/解读问题，不是未解决的容量 blocker。** Runtime PR #98 已通过全部 19 项 CI 并以 `92f6b62d` 合并。反复出现的 `16,383 / 16,384 bytes` 仅指专用静态 IRAM 链接器分类；对应的 D/IRAM 数据窗口仍约有 270 KB 余量。除非新构建提供相反的链接器窗口证据，否则不要再将其作为“总 IRAM 只剩 1 byte”重新排查。

> 最后更新：2026-11-14；适用版本：0.6.0-dev。范围：将经审查的实验能力纳入受治理的 Core/Runtime 路径，并退役由宿主负责文本度量与绘制的兼容接口。

## 当前检查点

- Runtime PR #71 已合并为 `50170381`，在 full/cropped 已安装消费者矩阵中资格验证 Core `b0bfd834` / ABI 4。合并后的 Runtime workflow [43462107068](https://github.com/xiaopaoya/JellyFrame/actions/runs/43462107068) 已通过。
- Runtime PR #88 已合并为 `e33fc03c`，PR 的 19 项检查全部通过。current-mainline full/cropped installed-consumer lane 固定到 Core PR #58 merge `be876270` / ABI4 / 163 个输入 / source identity `c804cdf023750de7feaf6c73ac841285e18b53b8468ffac6d99d2946615f27d8`；本地 MSVC Release 两种 profile 均通过 4/4。合并后的 master workflow [43556575072](https://github.com/xiaopaoya/JellyFrame/actions/runs/43556575072) 也已通过。Runtime production defaults 与签名 ABI3 依赖保持不变。
- Runtime PR #89 已合并为 `dfa68d44`，CI 全部通过：Windows ABI4 source-override lane 固定到当前 Core `be876270`，并运行正向增量/全量重绘矩阵；合并后 workflow [43563218601](https://github.com/xiaopaoya/JellyFrame/actions/runs/43563218601) 已通过。Runtime PR #90 已合并为 `ae9379d3`，增加 GDI/AppFontSet/CoreText 后端身份报告和显式路由测试，保留原 GDI 默认；合并后 workflow [43562778631](https://github.com/xiaopaoya/JellyFrame/actions/runs/43562778631) 已通过。
- Runtime PR #91 已合并为 `d0be03d5`，19 项 CI 全部通过；合并后的 master workflow [43564714754](https://github.com/xiaopaoya/JellyFrame/actions/runs/43564714754) 已通过。它从 launcher 自带 HTML/CSS 构建静态和路由态 host fixture，对 7px 标签做逐项可见像素断言并比较增量/全量帧。路由 shim 模拟 active-view class 转换，但没有执行 ESP32 native listener，因此只验证 Core layout/repaint 状态，不验证设备路由派发。
- Runtime PR #92 已合并为 `1a35bd75`，PR 检查全部通过。移植与架构指南现在将 Core `TextSystem` 定为新 port 的规范文本路径，并将宿主测量/绘制 provider 标记为迁移兼容接口。这是文档契约；provider 仍有生产调用方，尚未删除，生产默认值也未切换。
- Runtime PR #97 已合并为 `d28a5c2f`，CI workflow [43577992624](https://github.com/xiaopaoya/JellyFrame/actions/runs/43577992624) 的 19 项检查全部通过。它从 ESP32 image resolver 使用的同一不可变资源包读取并校验 BMP 固有尺寸，保持 metadata 转像素缓存后的同一 handle 用于绘制，并拒绝缺失、非法、超过 96px 或超资源预算的输入。仅当 Core 暴露 natural-image layout API 时才注册对应 metadata 测试；ABI4 CI job 使用 Core `be876270` 实际构建并运行该测试。ESP-IDF v5.3.1 CoreText 关闭/开启构建分别通过，镜像大小 5,445,568 B（app 余量 35%）和 7,614,192 B（余量 9%）。尚未刷写，固有尺寸实机验收仍未完成。
- 新的 WS147 CoreText ABI4 构建位于 `D:\CodexTemp\abi4-ws147-coretext-master-build-20261114-fresh`，使用 Runtime `d28a5c2f` 与 Core `0.9.0-dev` / ABI4 / 163 个输入 / source identity `c804cdf023750de7feaf6c73ac841285e18b53b8468ffac6d99d2946615f27d8`。BIN 为 7,606,768 B，SHA-256 `1EC3888E3927D87A699EA1027EBC7074B26473BBF4120E085DD77D61A493483D`；同一次构建生成的 16 MiB factory image SHA-256 `760944F4065B552AF275C31DBAA5B4821B6B7FDB0BDCAA0815D7CF06387B8E85`。ABI4 Developer Image package 已记录 Core `0.9.0-dev` / ABI 4 及两个镜像 hash，8 MiB app 分区余量 9%。均未刷写或实机验收。
- Developer Image provenance 现在通过 SHA-256 绑定 ESP-IDF `app_bin`，记录 Core 源码 identity/输入数、IDF 版本与 revision、toolchain、target、启用的构建 profile 及生效后的 `sdkconfig` hash。重新生成的 archive 位于 `D:\CodexTemp\abi4-ws147-coretext-master-build-20261114-fresh\provider-release-provenance-fixed\jellyframe-ws147-developer-0.6.2-ws147.abi4.1-provider-0.1.1-dev-provider-0.1.1-dev.zip`，其中 7,606,768 字节 BIN 的 SHA-256 与 manifest 一致；这证明 artifact identity，不代表设备验收。
- launcher Weather 重绘回归现在逐项检查 8 行预报文字。在固定 Core `0.9.0-dev` / ABI4 的本地桌面 shell 上，`launcher-weather-route` 的 legacy/CoreText 两条路径共 18 次增量/全量确定性捕获全部通过。Core 真实生成的 Noto 资源也通过直接 7/8px 测量、准备与光栅测试。这些主机门槛缩小了旧缺字反馈的排查范围，但不构成设备资格。
- Core PR #54 已合并为 `de9733d2`，修复了有限范围的离线 shaping 原型，并记录确定性数据编码实验。合并后的 Core workflow [43462040656](https://github.com/xiaopaoya/JellyFrame-Render-Core/actions/runs/43462040656) 已通过。它没有增加 Core package 实现输入，也没有增加运行时 shaping 能力。
- Core PR #55 已合并为 `64c1cb42`：移除了没有生产调用方的 `HostTextAdapter` wrapper/header，但保留仍有生产消费者的 provider 接口。Core CI 已通过。随后 Runtime workflow [43472234209](https://github.com/xiaopaoya/JellyFrame/actions/runs/43472234209) 的 ABI4 full 与 cropped lane 均明确检出 `64c1cb42`；两套 installed-consumer 测试通过，并报告 163 个实现输入对应的 source identity `213ff3196864f2d7da9152bf246c29bda9849da50daff272baf551a7f9179cc5`。此前 `b0bfd834` identity 仅是清理前基线。
- Core PR #56 合入为 `d9cdb239`，ABI4 source identity 为 `c1af69d7…23a2036f`，共 163 个输入。Runtime PR #81（`e410e4c9`）新增了固定该身份的 full/cropped installed-consumer lane，并在 workflow [43522375005](https://github.com/xiaopaoya/JellyFrame/actions/runs/43522375005) 通过。Core PR #57 合并后，Runtime PR #85 的合并后 workflow [43542749129](https://github.com/xiaopaoya/JellyFrame/actions/runs/43542749129) 虽然通过，但日志仍检出旧的 `d9cdb239`，没有资格验证新的 Core master。随后更新到 Core `9e19766c` / ABI4 / 163 个输入 / source identity `0e5000a6…d4e1366c` 的 pin 作为历史保留；Core PR #58 后，current-mainline 资格身份已更新为新 master。
- Runtime PR #83 已合并为 `eb68fe43`，19 项 CI 全部通过。无关图片的 paint-only 更新会改变全局 document mutation generation，从而错误使所有文字缓存失效；修复已从 Core 文字缓存有效性判断中移除该全局 generation。缓存仍按文本节点/文本、解析后的样式与几何、viewport，以及 TextSystem 身份/代次验证。full/cropped ABI4 lane 继续固定验证 Core `d9cdb239` / ABI4 / 163 个输入 / source hash `c1af69d7…23a2036f`。
- Runtime PR #72 已合并为 `5c45c65a`；新增中英双语任务表，区分 ABI4 资格验证、生产采用、回调退役与设备验收。CI 已通过。
- Runtime PR #73 已合并为 `2c253566`；为带签名的 Core `0.8.0` / ABI3 archive 增加 checksum 校验的离线 bootstrap。Runtime CI 已通过；PR 前本地 67 项 CTest 也全部通过。bootstrap 让当前生产锁可复现，但不会切换到 ABI4，也没有建立默认字体目录。
- Runtime PR #75 已合并为 `6302dd1d`；ABI4 installed consumer、v10 与 R3 矩阵、Windows、standalone Core 和 ASan/UBSan CI 全部通过。该 PR 加入共享 Noto Sans SC 16 px Regular/Medium/Bold Core 字体目录、拥有/借用式事务 API、资源 provenance 工具和目录字重/生命周期测试。
- B2 Runtime PR #76 已合并为 `2e89b0de`，全部 CI 通过。它将 Core 文本放在显式 `--use-app-fonts` 验证路径中，兼容门槛通过前保留 GDI 默认与旧 JFFONT 绘制。B1 Runtime PR #77 已合并为 `af594be4`，全部 CI 通过；full/cropped Core source 主机配置均通过 6/6 聚焦 CTest，覆盖已安装资源所有权、四种 CoreText/BMP 组合及 App Runtime。WS147 命名 profile 已由 ESP-IDF v5.3.1 构建成功，镜像 7,395,718 字节，在 8 MiB app 分区中余量 12%；同代码 band-shell 基线为 5,230,594 字节、余量 38%，profile 增加 2,165,124 字节。两种构建的 IRAM 均为 16,383/16,384 字节，说明近满载是已有基线状态，并非 Core text 新增。当前未枚举到 WS147 串口，尚未实机验收。
- Runtime 生产依赖仍锁定带签名的 Core `0.8.0` / ABI 3。ABI4 尚无签名发布，也不是生产依赖。PR #58 后 Core master 为 `be876270`（ABI4、163 个实现输入，source manifest `c804cdf023750de7feaf6c73ac841285e18b53b8468ffac6d99d2946615f27d8`）；PR #88 已通过对应 full/cropped installed-consumer CI。workflow `43542749129` 与 `9e19766c` pin 仅作为历史记录。
- 页面路由回归最初定位到 `valid_text_layout_cache`：dirty ancestor 会一直保留到 present，因此 layer builder 拒绝当前布局刚生成的文字缓存。Core PR #56 将文档 generation 纳入当前 layout transaction 的缓存快照，以识别本轮刚生成的文字，并加入 `display:none` 首页/Activity 回归。Runtime PR #83（`eb68fe43`）另行修复 Runtime 自身 paint-only 图片更新路径，并通过 240 帧增量/全量比较。之后，安装 Core 的桌面探针发现 standalone Core 仍会因无关 input value 更新而使静态 prepared text 失效。Core PR #57（`9e19766c`）移除此缓存有效性判断中的全局文档 generation 并增加回归；安装源码清单 hash 为 `0e5000a685842df82b16ff1a6aa0d3a09844e07e9850deff939912d4e1366c3c`，共 163 个输入。Runtime PR #85（`7c0c9197`）加强正向 Core repaint gate，防止布局拒绝导致两帧同为空白仍误判等价。Core PR #57 CI 已通过；Release full CTest 33/33、standalone 安装消费者 1/1，配对 Runtime 桌面门槛 12 条路径共 240 帧一致。Cropped CTest 为 31/32；唯一 shared-inline 断言在未修改的 `d9cdb239` 同配置下也复现。
- 最新实机反馈仍是 launcher Weather 页四张预报小卡缺少内部文字；此前照片确认这是 `band_shell.html` 系统 shell 路由，不是独立的 `samples/apps/packages/watch_weather` 应用。该次观察对应的 ELF/backend/profile 未与已验证 manifest 绑定，故仍是未解决的设备验收问题。固定 Core `0.9.0-dev` / ABI4 的本地 shell 已逐帧确认 8 行 7px 文本可见，Core 真实 Noto 资源 7/8px 测量、准备和光栅测试通过。Runtime 工作树新增了直接调用 ESP32 与 host 共用 native route helper 的 CTest，覆盖真实 launcher HTML 上 Weather/Back 事件及 DOM 文本保留；这只验证事件映射和 DOM 状态，不验证排版绘制。须以 provenance-bound ABI4 镜像实机验收，判断剩余问题是否为设备 layout/paint 行为。
- Runtime PR #80（`dbe4ca05`）修复了路由文本缓存代次、34px 字号行高，并移除三字面目录上限；17 项 CI 通过。此前 ABI3 staged 镜像曾在板上启动成功，但后续用户报告时钟模糊/裁切和切页文字消失，不能将早期启动日志或桌面截图视为这些问题已解决；当前 ABI4 镜像尚未实机验收。
- 桌面 PR #79（`45c6677d`）增加 `letter-spacing` 重绘等价门槛。Runtime PR #83 修复 paint-only 图片失效，Core PR #57 修复无关全局 generation 导致的缓存失效，Runtime PR #85 增加文字正向可见断言与 input mutation 重绘回归。主机 CI 不能证明 WS147 固件身份或实机验收。
- 生产默认仍未切换：WS147 默认走 callback/v6，CoreText 需选择命名 profile；桌面默认仍为 GDI。`TextMeasureProvider` 和 `TextPainter` 因仍有生产消费者而保留；仅移除了未使用的 `HostTextAdapter` wrapper。PR #92 将 Core `TextSystem` 定为新 port 的规范路径，并把 provider 归类为迁移兼容接口。fresh ABI4 build/package 记录于上文，尚未刷写，设备资格仍待完成。IRAM 结论以本计划顶部的 PR #98 状态为准。
- 新 ABI4 Developer Image 已有源码/构建 provenance，但尚无 ABI4 镜像在 WS147 上刷写并验收。此前刷写的 CoreText 镜像采用 staged ABI3 依赖，不满足 ABI4 设备准入。
- Core PR #38–47 已继续合入 shared-inline 装饰线、混合字体 owner、保留空白、生成文本和列表标记等有限切片。它们只交付文档声明的范围，不代表一般 shaping、Unicode、正 blur 或折叠 layer fidelity 已实现。
- Core 主线已包含获准的 F6 文本样式快路径。成对 host 测量在文本密集 fixture 上记录 17.939% 降幅（95% CI 17.267%–18.610%）；全元素对照区间跨零。该证据只适用于指定 workload，不代表 MCU 或整应用收益；不要重新移植旧候选分支。

## 可并行工作流

### A. 资源交付与字体目录默认路径

Runtime PR #73 已建立当前签名 Core package 的干净 checkout 与 checksum 校验 bootstrap。Runtime PR #75 已合入共享默认资源：Noto Sans SC 16 px Regular/Medium/Bold、4bpp、每个字面覆盖 4,539 个码点，许可证为 OFL 1.1。共享 API 支持拥有副本与稳定借用视图。桌面和 WS147 消费者已分别通过 PR #76、#77 合并。字体目录为 2,159,771 字节；IDF v5.3.1 下，命名 Core-text profile 镜像为 7,395,718 字节、app 分区余量 12%；同代码 band-shell 基线为 5,230,594 字节、余量 38%，profile 增加 2,165,124 字节。两者 IRAM 均为 16,383/16,384 字节，说明近满载是已有状态，并非 Core text 引入。Runtime 持有应用资源；Core 负责字体选择、度量、shaping 和字形准备。

**退出证据：**全新 checkout 不依赖用户本地文件即可构建；full/cropped/禁用特性 profile 通过；package identity 与生成 provenance 一致；资源及内存预算已记录并有测试。

### B. Runtime 消费者迁移（资源契约固定后并行）

- **B1 原生 UI 与已安装应用 worker（PR #77 已合并）：**将保留式 UI 和已安装 worker 资源接入共享字体目录，并新增命名 `sdkconfig.ws147_core_text.defaults` profile，生产 Kconfig 默认值保持不变。WS147 常规路径仍创建 `TextMeasureProvider`/`TextPainter` 回调；CoreText profile 才会绕过它们。full/cropped 主机组合、PR CI 与 IDF v5.3.1 构建均通过。ABI3 staged profile 与单独 ABI4 候选都仍需实机验收。
- **B2 桌面（PR #76 分阶段；资格 PR #79/#89/#90 已合并）：**已提供显式 Core catalog 验证路径和 JFFONT2 补充字体，同时保留 GDI 默认与旧 JFFONT。PR #89 已在 Windows ABI4 source-override lane 运行带正向断言的增量/全量重绘矩阵；PR #90 增加后端选择诊断与路由测试。Core 的 bounded `letter-spacing` 实现通过 Release CTest 和真实字体门槛，但 exact-Core Runtime 可见输出/重绘测试目前失败：确认点击脚本、style/layout 失效和 CoreText 后端均正常，0px→2px 样式变化后文字仍消失。这是合入阻断；修复并通过门槛前不得合入或切换默认。ellipsis 仍不支持，且没有逐节点 fallback。旧 JFFONT 仍需要 AppFontSet 回调。

**退出证据：**完成属性兼容门槛后，内置 catalog 默认使用 CoreText；回调路径仅保留给明确记录的旧 JFFONT 兼容；已安装 package 与 source override 的图像一致；应用字体和损坏资源拒绝测试通过。

### C. Core 自有文本 API 收敛

待 B 的消费者转用 Core 持有的字体目录后，让 Core layout、intrinsic sizing、layer 构建、repaint/reuse、rasterizer 和 Canvas2D 直接使用 `TextSystem`/拥有所有权的文本服务。移除依赖回调身份的缓存契约，并迁移回调测试、fixtures、source archive 测试和移植文档。已核实的生产调用方包括 ESP32 UI/已安装应用 JFFONT 回调、Win32 默认 GDI 度量/绘制、桌面旧 JFFONT 的 AppFontSet，以及 embedded-host sample。PR #55 已单独移除 `HostTextAdapter`：全仓清单没有找到它的生产调用方。这不代表底层 `TextMeasureProvider`、`TextPainter`、range/family/additive hooks 或 fallback 实现已退役；剩余接口须等所有消费者迁移后，在首次正式 ABI4 release 前协调移除。

**退出证据：**仓库范围生产路径搜索不再发现有意保留的旧兼容以外的回调路径；删除回调 API 后 Core 全套、source archive、full/cropped 已安装消费者和禁用特性 profile 均通过；公开移植指南只描述 Core 自有字体契约。实机资格是后续准入门槛，不是 API 删除的前置条件。

### D. 发布与 Runtime 生产锁

只从最终审查通过的 API/资源契约创建带签名的确定性 Core release。验证 source archive checksum、manifest、feature profile、standalone build/install/tests，然后更新 Runtime 的精确版本/ABI/source lock 及发布 artifact CI。保留 ABI3 lane 作为迁移/回滚证据；不得声称 ABI3 和 ABI4 可二进制互换。

**退出证据：**签名 tag 与 archive 已发布；同一 revision 的 Runtime installed-package 和 source-override 测试通过；CI 与中英文兼容性/发布文档一致。

### E. Device OS 准入（下游）

把最终 Runtime/Core provenance 和所选 feature families 绑定到命名的 WS147 profile 与 firmware manifest。在该镜像上执行实机已安装应用、文字/图片、输入、生命周期/恢复、内存、并发和性能验收。现有 ABI1/ABI3 镜像证据不能用于 ABI4 资格声明。

**退出证据：**可复现镜像 manifest 和原始设备日志记录准确 Core release/profile/source hash；适用的移植 gate 全部通过。这是设备采用的必要条件，但可以在主机侧迁移合入并发布之后开展。

## 依赖顺序

```text
A1 bootstrap 已完成；A2 字体目录/API 已合并（#75）
       |------------------|
       v                  v
B1 原生 UI/worker       B2 桌面
       \                  /
        \                /
         v              v
       C 移除回调 API
                 |
                 v
       D Core 签名发布 + Runtime 锁
                 |
                 v
       E 命名设备 profile + 实机验收
```

A/B 期间可以并行审计 Core API 和迁移测试，但必须等所有生产消费者迁移后才能删除公共 API。Shaping lookup 编码仍是独立实验：目前的字节数不是固件成本或运行时资格，也不是集成已通过 ABI4 能力的前置条件。

## 近期动作

1. **已完成：**PR #54、#55、#71、#72、#73 均已合并；Runtime 签名 ABI3 source bootstrap 可复现，Core 未使用的 wrapper 已移除。
2. **已完成：**Runtime full/cropped ABI4 资格验证现已覆盖 Core `64c1cb42` 和 source identity `213ff3196864f2d7da9152bf246c29bda9849da50daff272baf551a7f9179cc5`（163 个实现输入），workflow 为 `43472234209`。
3. **已完成：**PR #75 的共享字体目录/API 在全部 CI 通过后合并。
4. **主机门槛通过，板级症状仍未归因：**Runtime PR #83、Core PR #57、Runtime PR #85 已合并且 CI 通过。设备照片确认 launcher Weather 大卡和空气质量提示有字，四个 forecast 小卡只有图标；这与 `watch_weather` 示例不同。PR #91 的 host route shim 没有执行 ESP32 native listener；新增 CTest 使用与 ESP32 相同的 native route helper，验证事件映射和 forecast DOM 文本保留。固定 Core `0.9.0-dev` / ABI4 的本地 shell 已逐项确认 8 行 7px 小卡文字可见；Core 真实 Noto 资源的 7/8px 测量、准备和光栅测试也通过。ABI4 Developer Image manifest 已将构建 provenance 与实际 app BIN 绑定；镜像仍未刷写或实机验收。旧首页时钟模糊/裁切是独立问题。
5. **桌面兼容待办：**Core PR #60 增加 bounded prepared-text `letter-spacing` 和 JFTP v3 显式字形位置，同时保持 v1/v2 解码。Release CTest、真实字体 gate、exact-Core Runtime 可见输出及增量/全量重绘检查均通过，PR CI 正在运行。PR #60 合入后，将其 Core source identity 固定到 Runtime ABI4 full/cropped lane。ellipsis 仍待实现或增加充分测试的逐节点 fallback；这之前桌面默认保留 GDI。
6. **接口与消费者迁移：**ESP32 native route mapping/state transition 共享主机回归已通过；下一步以 provenance-bound ABI4 WS147 CoreText profile 进行实机资格验证，迁移 native UI/installed-app worker provider、桌面 GDI/AppFontSet，并决定 embedded-host demo 是否保留为兼容示例。所有消费者与 callback 测试迁移前，不移除 `TextMeasureProvider`/`TextPainter`。
7. 主机消费者及测试迁移完成后，再协调删除 callback API、最终 Core ABI4 release/archive、Runtime package-lock/default adoption。命名 ABI4 设备 provenance 和实机验收是其后的 device-adoption gate，不阻塞主机侧 API 删除。ABI4 full/cropped CI 只证明 installed-consumer 兼容性，不等于签名发布或实机验收。
