# Render Core ABI4 集成计划

> 最后更新：2026-11-14；适用版本：0.6.0-dev。范围：将经审查的实验能力纳入受治理的 Core/Runtime 路径，并退役由宿主负责文本度量与绘制的兼容接口。

## 当前检查点

- Runtime PR #71 已合并为 `50170381`，在 full/cropped 已安装消费者矩阵中资格验证 Core `b0bfd834` / ABI 4。合并后的 Runtime workflow [43462107068](https://github.com/xiaopaoya/JellyFrame/actions/runs/43462107068) 已通过。
- Runtime PR #88 已合并为 `e33fc03c`，PR 的 19 项检查全部通过。current-mainline full/cropped installed-consumer lane 固定到 Core PR #58 merge `be876270` / ABI4 / 163 个输入 / source identity `c804cdf023750de7feaf6c73ac841285e18b53b8468ffac6d99d2946615f27d8`；本地 MSVC Release 两种 profile 均通过 4/4。合并后的 master workflow [43556575072](https://github.com/xiaopaoya/JellyFrame/actions/runs/43556575072) 也已通过。Runtime production defaults 与签名 ABI3 依赖保持不变。
- Runtime PR #89 已合并为 `dfa68d44`，CI 全部通过：Windows ABI4 source-override lane 固定到当前 Core `be876270`，并运行正向增量/全量重绘矩阵；合并后 workflow [43563218601](https://github.com/xiaopaoya/JellyFrame/actions/runs/43563218601) 已通过。Runtime PR #90 已合并为 `ae9379d3`，增加 GDI/AppFontSet/CoreText 后端身份报告和显式路由测试，保留原 GDI 默认；合并后 workflow [43562778631](https://github.com/xiaopaoya/JellyFrame/actions/runs/43562778631) 已通过。
- Runtime PR #91 已合并为 `d0be03d5`，19 项 CI 全部通过；合并后的 master workflow [43564714754](https://github.com/xiaopaoya/JellyFrame/actions/runs/43564714754) 已通过。它从 launcher 自带 HTML/CSS 构建静态和路由态 host fixture，对 7px 标签做逐项可见像素断言并比较增量/全量帧。路由 shim 模拟 active-view class 转换，但没有执行 ESP32 native listener，因此只验证 Core layout/repaint 状态，不验证设备路由派发。
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
- 最新实机反馈有设备照片确认：用户已从 launcher 导航至 Weather（画面为 `BACK / WEATHER / NOW`）；大天气卡和底部空气质量提示有文字，四个 forecast 小卡只见黄色图标，没有小时/温度标签。这是 `band_shell.html` 系统 shell 路由，不是独立的 `samples/apps/packages/watch_weather` 应用。本次观察对应的 ELF/backend/profile 尚未确认，因此故障原因仍未归属。Runtime PR #91 的静态和路由态 fixture 使用 launcher 原始 HTML/CSS，逐项断言 Weather 帧中的 7px 标签，并比较增量/全量输出；Core `0.9.0-dev` CI 已通过。Host 路由 shim 没有执行 ESP32 native listener，且没有复现照片中的缺字：class 转换后四个标签都能绘制。Core layout 接受 7px，但此前 Core 字号探针从 15.5px 开始；可独立补充 Core 7px 字形/度量覆盖。剩余关键差异是板级 native 路由/状态/绘制集成或硬件 profile，不是独立的 `watch_weather` 示例。此前首页时钟模糊/裁切是独立的字体度量/行高问题。
- WS147 Core 字体目录加入了紧凑的 34px 粗体数字字面；字面 hhea ascent+descent 在 34px 下约为 49.2px，此前首页时钟行高只有 36px。候选 CSS 已将两处大字行高设为 50px，172x320 桌面捕获和浏览器对照显示字形完整、首页无溢出。Runtime PR #80 已合并为 `dbe4ca05`，17 项 CI 全部通过；修复路由文本缓存代次判断、34px 行高，并移除拒绝扩展字体目录的三字面硬编码。ESP-IDF v5.3.1 staged CoreText band-shell 镜像为 7,399,152 字节，8 MiB app 分区剩余 989,456 字节（12%）；ELF SHA-256 `058CD0C45CAFD9BAEA7C6EC6C71F985F3B13C7BBDEF9414F7B18BE81F15FACC1`，BIN SHA-256 `EBAC43BDB563FB9462E1BEBF55A7C1D78C178706F103AB94909DF709F48B06F9`。镜像已刷入 WS147（COM19）；启动日志确认板型、`/band_shell.html`、字体包加载及 `first_present_ok=1`。用户对实际画面和页面切换的验收仍待回复。此 staged 镜像仍使用 Runtime 已签名 ABI3 依赖，不构成 ABI4 设备资格。
- 桌面 PR #79（`45c6677d`）已合并，增加了 Core `letter-spacing` 文本变更的重绘等价用例，检查实际选择的文本后端和像素确有变化，并逐字节比较增量重绘与强制全量重绘；聚焦重绘、shell CLI 和 app-shell workflow 本地测试通过。该门槛覆盖一个变更场景；切换桌面默认路径前仍需更广泛的支持属性覆盖。
- Runtime PR #83 修复了 Runtime 的 paint-only 图片缓存路径；Core PR #57 修复 standalone Render Core 中对应的全局 generation 失效；Runtime PR #85 加入文字非空断言和 input mutation 重绘回归。两个 PR 的 CI 均通过。这些主机验证不能证明 WS147 上的固件身份或实机验收结果。
- 当前生产默认仍未启用完整 Core 字体路径。WS147 默认配置仍使用回调/v6，Core-text 命名 profile 需显式选择；桌面默认仍为 GDI，Core 路径仅用于显式 opt-in 验证；PR #90 只增加后端诊断，没有切换默认。`TextMeasureProvider` 与 `TextPainter` 仍是 Core 公共 API，因为生产消费者仍在使用；目前只移除了没有生产调用方的 `HostTextAdapter` 便利层。移植文档正更新为：新 port 以 Core `TextSystem` 为规范路径，provider 仅标记为迁移期间的兼容接口。ABI4 WS147 CoreText 候选已构建并记录于 `E:\Builds\ws147-abi4-final-candidate-20261114\build-manifest.json`，但尚未刷写。8 MiB app 分区余量 9%，IRAM 只余 1 字节，因此它只是功能候选，不满足发布余量准入。
- 当前没有 ABI4 命名设备 profile 的 provenance 或 ABI4 固件验收记录。已刷写的 CoreText 镜像是 ABI3 依赖下的 staged profile，可证明集成运行，不满足 ABI4 设备准入。
- Core PR #38–47 已继续合入 shared-inline 装饰线、混合字体 owner、保留空白、生成文本和列表标记等有限切片。它们只交付文档声明的范围，不代表一般 shaping、Unicode、正 blur 或折叠 layer fidelity 已实现。
- Core 主线已包含获准的 F6 文本样式快路径。成对 host 测量在文本密集 fixture 上记录 17.939% 降幅（95% CI 17.267%–18.610%）；全元素对照区间跨零。该证据只适用于指定 workload，不代表 MCU 或整应用收益；不要重新移植旧候选分支。

## 可并行工作流

### A. 资源交付与字体目录默认路径

Runtime PR #73 已建立当前签名 Core package 的干净 checkout 与 checksum 校验 bootstrap。Runtime PR #75 已合入共享默认资源：Noto Sans SC 16 px Regular/Medium/Bold、4bpp、每个字面覆盖 4,539 个码点，许可证为 OFL 1.1。共享 API 支持拥有副本与稳定借用视图。桌面和 WS147 消费者已分别通过 PR #76、#77 合并。字体目录为 2,159,771 字节；IDF v5.3.1 下，命名 Core-text profile 镜像为 7,395,718 字节、app 分区余量 12%；同代码 band-shell 基线为 5,230,594 字节、余量 38%，profile 增加 2,165,124 字节。两者 IRAM 均为 16,383/16,384 字节，说明近满载是已有状态，并非 Core text 引入。Runtime 持有应用资源；Core 负责字体选择、度量、shaping 和字形准备。

**退出证据：**全新 checkout 不依赖用户本地文件即可构建；full/cropped/禁用特性 profile 通过；package identity 与生成 provenance 一致；资源及内存预算已记录并有测试。

### B. Runtime 消费者迁移（资源契约固定后并行）

- **B1 原生 UI 与已安装应用 worker（PR #77 已合并）：**将保留式 UI 和已安装 worker 资源接入共享字体目录，并新增命名 `sdkconfig.ws147_core_text.defaults` profile，生产 Kconfig 默认值保持不变。WS147 常规路径仍创建 `TextMeasureProvider`/`TextPainter` 回调；CoreText profile 才会绕过它们。full/cropped 主机组合、PR CI 与 IDF v5.3.1 构建均通过。ABI3 staged profile 与单独 ABI4 候选都仍需实机验收。
- **B2 桌面（PR #76 已合并，分阶段）：**加入显式 Core catalog 验证路径与 JFFONT2 应用补充字体，同时保留 GDI 默认和旧 JFFONT bitmap 路径。GDI 默认路径提供度量与绘制回调；旧 JFFONT 仍经由 AppFontSet 回调。首次无条件启用 Core 时，既有字体策略与增量重绘等价测试失败，并拒绝了 `letter-spacing`；这些结果确定了兼容门槛，不能视为测试噪音。现有重绘门槛覆盖五类 CSS 文本属性以及动画、路由、天气、滚动和 watch-face 流程，验证实际像素发生变化，并逐帧对比全量重绘。此前该门槛没有在 Windows ABI4 source-override CI job 中运行；PR #89 已将其接入。该 lane 在精确 Core 源码身份上通过后，再将桌面内置 catalog 文本切到 CoreText，JFFONT fallback 暂时保留。

**退出证据：**没有生产默认路径再选择回调度量/绘制；已安装 package 与 source override 的图像一致；应用字体和损坏资源拒绝测试通过。

### C. Core 自有文本 API 收敛

待 B 的消费者转用 Core 持有的字体目录后，让 Core layout、intrinsic sizing、layer 构建、repaint/reuse、rasterizer 和 Canvas2D 直接使用 `TextSystem`/拥有所有权的文本服务。移除依赖回调身份的缓存契约，并迁移回调测试、fixtures、source archive 测试和移植文档。已核实的生产调用方包括 ESP32 UI/已安装应用 JFFONT 回调、Win32 默认 GDI 度量/绘制、桌面旧 JFFONT 的 AppFontSet，以及 embedded-host sample。PR #55 已单独移除 `HostTextAdapter`：全仓清单没有找到它的生产调用方。这不代表底层 `TextMeasureProvider`、`TextPainter`、range/family/additive hooks 或 fallback 实现已退役；剩余接口须等所有消费者迁移后，在首次正式 ABI4 release 前协调移除。

**退出证据：**仓库范围生产路径搜索不再发现旧回调；移除回调后 Core 全套、source archive、full/cropped 已安装消费者和禁用特性 profile 均通过；公开移植指南只描述 Core 自有字体契约。

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
4. **主机资格门槛已合入，实机问题仍未归因：**Runtime PR #83、Core PR #57、Runtime PR #85 已合并；#89 固定 current-Core repaint matrix，#90 加入桌面后端身份诊断，#91 覆盖 launcher Weather 源页面和 class 路由态。设备照片确认大卡和空气质量提示有字，四个 forecast 小卡只显示图标；与 `watch_weather` 示例不同。#91 的 host shim 没执行 ESP32 native listener，且四个标签在 class 切换后都能绘制。实机 ELF/backend/profile 尚未确认；需让当前镜像可追溯，再调查板级 listener/状态/绘制差异。7px 标签仍无直接 Core 字号探针（现有从 15.5px 开始），可并行补充 Core 字形/度量 coverage。ABI4 候选尚未刷板，Core `0.9.0-dev` source override，app 分区余量 9%、IRAM 余量 1 字节。
5. **下一迁移切片：**Core `TextSystem` 仍拒绝 `letter-spacing` 和 ellipsis 文本且无逐节点 fallback，因此桌面默认保留 GDI。先补 Core 7px/8px 字形与度量测试，再解决这些属性的 Core/旧后端一致性并跑 exact-Core full/cropped 重绘门槛；之后再评估桌面默认采用。
6. **接口与消费者迁移：**移植文档将更新为新 port 使用 Core `TextSystem`，provider callback 仅是现有消费者迁移期间的兼容接口。并行推进 ESP32 callback/v6 与 installed-app JFFONT、桌面 GDI/AppFontSet、embedded-host demo 清单；所有消费者和测试迁移之前不移除 `TextMeasureProvider`/`TextPainter`。
7. 桌面/设备消费者验收后，再协调 callback API 移除、最终 Core ABI4 release/archive、Runtime package-lock/default adoption 和命名 ABI4 设备资格验证。ABI4 full/cropped CI 只证明 installed-consumer 兼容性，不等于签名发布或实机验收。
6. 桌面/设备消费者验收后，再协调 callback API 移除、最终 Core ABI4 release/archive、Runtime package-lock/default adoption 和命名 ABI4 设备资格验证。ABI4 full/cropped CI 只证明 installed-consumer 兼容性，不等于签名发布或实机验收。
