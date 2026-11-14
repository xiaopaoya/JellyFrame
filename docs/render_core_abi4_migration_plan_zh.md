# Render Core ABI4 集成计划

> 最后更新：2026-11-14；适用版本：0.6.0-dev。范围：将经审查的实验能力纳入受治理的 Core/Runtime 路径，并退役由宿主负责文本度量与绘制的兼容接口。

## 当前检查点

- Runtime PR #71 已合并为 `50170381`，在 full/cropped 已安装消费者矩阵中资格验证 Core `b0bfd834` / ABI 4。合并后的 Runtime workflow [43462107068](https://github.com/xiaopaoya/JellyFrame/actions/runs/43462107068) 已通过。
- Core PR #54 已合并为 `de9733d2`，修复了有限范围的离线 shaping 原型，并记录确定性数据编码实验。合并后的 Core workflow [43462040656](https://github.com/xiaopaoya/JellyFrame-Render-Core/actions/runs/43462040656) 已通过。它没有增加 Core package 实现输入，也没有增加运行时 shaping 能力。
- Core PR #55 已合并为 `64c1cb42`：移除了没有生产调用方的 `HostTextAdapter` wrapper/header，但保留仍有生产消费者的 provider 接口。Core CI 已通过。随后 Runtime workflow [43472234209](https://github.com/xiaopaoya/JellyFrame/actions/runs/43472234209) 的 ABI4 full 与 cropped lane 均明确检出 `64c1cb42`；两套 installed-consumer 测试通过，并报告 163 个实现输入对应的 source identity `213ff3196864f2d7da9152bf246c29bda9849da50daff272baf551a7f9179cc5`。此前 `b0bfd834` identity 仅是清理前基线。
- Core PR #56 已合并为 `d9cdb239`。从当前 Core master 新鲜导出并重新配置后，身份为 ABI 4、163 个实现输入、source hash `c1af69d7a0fc1b63fe5339c0300438be521a5b18d9807a3265bc487123a2036f`。Runtime PR #81（`e410e4c9`）新增了固定此身份的 full/cropped installed-consumer lane；两条均在 workflow [43522375005](https://github.com/xiaopaoya/JellyFrame/actions/runs/43522375005) 通过。此前资格身份保留为历史证据，不能替代当前主线验证。
- Runtime PR #72 已合并为 `5c45c65a`；新增中英双语任务表，区分 ABI4 资格验证、生产采用、回调退役与设备验收。CI 已通过。
- Runtime PR #73 已合并为 `2c253566`；为带签名的 Core `0.8.0` / ABI3 archive 增加 checksum 校验的离线 bootstrap。Runtime CI 已通过；PR 前本地 67 项 CTest 也全部通过。bootstrap 让当前生产锁可复现，但不会切换到 ABI4，也没有建立默认字体目录。
- Runtime PR #75 已合并为 `6302dd1d`；ABI4 installed consumer、v10 与 R3 矩阵、Windows、standalone Core 和 ASan/UBSan CI 全部通过。该 PR 加入共享 Noto Sans SC 16 px Regular/Medium/Bold Core 字体目录、拥有/借用式事务 API、资源 provenance 工具和目录字重/生命周期测试。
- B2 Runtime PR #76 已合并为 `2e89b0de`，全部 CI 通过。它将 Core 文本放在显式 `--use-app-fonts` 验证路径中，兼容门槛通过前保留 GDI 默认与旧 JFFONT 绘制。B1 Runtime PR #77 已合并为 `af594be4`，全部 CI 通过；full/cropped Core source 主机配置均通过 6/6 聚焦 CTest，覆盖已安装资源所有权、四种 CoreText/BMP 组合及 App Runtime。WS147 命名 profile 已由 ESP-IDF v5.3.1 构建成功，镜像 7,395,718 字节，在 8 MiB app 分区中余量 12%；同代码 band-shell 基线为 5,230,594 字节、余量 38%，profile 增加 2,165,124 字节。两种构建的 IRAM 均为 16,383/16,384 字节，说明近满载是已有基线状态，并非 Core text 新增。当前未枚举到 WS147 串口，尚未实机验收。
- Runtime 生产依赖仍锁定带签名的 Core `0.8.0` / ABI 3。ABI4 尚无签名发布，也不是生产依赖；Core master `d9cdb239` 在 C、D 退出条件满足前仅作为资格验证目标。
- 页面路由回归已定位到 `valid_text_layout_cache`：dirty ancestor 会一直保留到 present，因此 layer builder 把当前布局刚生成的文字缓存也拒绝了。Core PR #56 已合并，改为用 document mutation generation 判断缓存新旧，并加入 `display:none` 首页/Activity 路由回归；独立 Core Release CTest 为 33/33。Runtime 镜像修复已通过 Core text-system 与 App Runtime 主机聚焦测试（2/2）。
- 最近一次实机反馈称，运行“合并主线固件”时切页后文字消失，首页时钟模糊且有裁切。此次固件的精确 ELF、所选文本后端/profile，以及问题发生在 Activity、Weather 还是两者，尚未记录。先作为未解决的设备回归处理，不能据此断言 PR #56 的缓存修复无效，也不能断定它与旧故障完全同因；应先核验固件身份和路由表现，再用匹配的主机 profile 复现。
- WS147 Core 字体目录加入了紧凑的 34px 粗体数字字面；字面 hhea ascent+descent 在 34px 下约为 49.2px，此前首页时钟行高只有 36px。候选 CSS 已将两处大字行高设为 50px，172x320 桌面捕获和浏览器对照显示字形完整、首页无溢出。Runtime PR #80 已合并为 `dbe4ca05`，17 项 CI 全部通过；修复路由文本缓存代次判断、34px 行高，并移除拒绝扩展字体目录的三字面硬编码。ESP-IDF v5.3.1 staged CoreText band-shell 镜像为 7,399,152 字节，8 MiB app 分区剩余 989,456 字节（12%）；ELF SHA-256 `058CD0C45CAFD9BAEA7C6EC6C71F985F3B13C7BBDEF9414F7B18BE81F15FACC1`，BIN SHA-256 `EBAC43BDB563FB9462E1BEBF55A7C1D78C178706F103AB94909DF709F48B06F9`。镜像已刷入 WS147（COM19）；启动日志确认板型、`/band_shell.html`、字体包加载及 `first_present_ok=1`。用户对实际画面和页面切换的验收仍待回复。此 staged 镜像仍使用 Runtime 已签名 ABI3 依赖，不构成 ABI4 设备资格。
- 桌面 PR #79（`45c6677d`）已合并，增加了 Core `letter-spacing` 文本变更的重绘等价用例，检查实际选择的文本后端和像素确有变化，并逐字节比较增量重绘与强制全量重绘；聚焦重绘、shell CLI 和 app-shell workflow 本地测试通过。该门槛覆盖一个变更场景；切换桌面默认路径前仍需更广泛的支持属性覆盖。
- 后续 Core 重绘检查发现，无关图片的 paint-only 变更会递增全局 document mutation generation，从而使所有布局持有的文本缓存失效。增量重绘保留了旧文字像素，全量重绘则暴露出缺失的 Core 文字命令。现在缓存有效性改为依据文本节点及已解析的布局输入判定；Core 单测和桌面等价门槛已通过，覆盖 240 帧及两种文本后端。该修复目前在本地，仍待 PR/CI 审查。
- 当前生产默认仍未启用完整 Core 字体路径。WS147 默认配置仍使用回调/v6，Core-text 命名 profile 需显式选择；桌面默认仍为 GDI，Core 路径仅用于显式 opt-in 验证。`TextMeasureProvider` 与 `TextPainter` 仍是 Core 公共 API，因为生产消费者仍在使用；目前只移除了没有生产调用方的 `HostTextAdapter` 便利层。
- 当前没有 ABI4 命名设备 profile 的 provenance 或 ABI4 固件验收记录。已刷写的 CoreText 镜像是 ABI3 依赖下的 staged profile，可证明集成运行，不满足 ABI4 设备准入。

## 可并行工作流

### A. 资源交付与字体目录默认路径

Runtime PR #73 已建立当前签名 Core package 的干净 checkout 与 checksum 校验 bootstrap。Runtime PR #75 已合入共享默认资源：Noto Sans SC 16 px Regular/Medium/Bold、4bpp、每个字面覆盖 4,539 个码点，许可证为 OFL 1.1。共享 API 支持拥有副本与稳定借用视图。桌面和 WS147 消费者已分别通过 PR #76、#77 合并。字体目录为 2,159,771 字节；IDF v5.3.1 下，命名 Core-text profile 镜像为 7,395,718 字节、app 分区余量 12%；同代码 band-shell 基线为 5,230,594 字节、余量 38%，profile 增加 2,165,124 字节。两者 IRAM 均为 16,383/16,384 字节，说明近满载是已有状态，并非 Core text 引入。Runtime 持有应用资源；Core 负责字体选择、度量、shaping 和字形准备。

**退出证据：**全新 checkout 不依赖用户本地文件即可构建；full/cropped/禁用特性 profile 通过；package identity 与生成 provenance 一致；资源及内存预算已记录并有测试。

### B. Runtime 消费者迁移（资源契约固定后并行）

- **B1 原生 UI 与已安装应用 worker（PR #77 已合并）：**将保留式 UI 和已安装 worker 切至共享字体目录，并新增命名 `sdkconfig.ws147_core_text.defaults` profile，同时保持生产 Kconfig 默认值不变。full/cropped 主机组合、PR CI 与 IDF v5.3.1 固件构建均已通过；镜像为 7,395,718 字节，app 分区余量 12%。仍需 WS147 实机验收；本次未枚举到开发板串口。
- **B2 桌面（PR #76 已合并，分阶段）：**加入显式 Core catalog 验证路径与 JFFONT2 应用补充字体，同时保留当前 GDI 默认和旧 JFFONT bitmap 路径。首次无条件启用 Core 时，既有字体策略与增量重绘等价测试失败，并拒绝了 `letter-spacing`；这些结果确定了兼容门槛，不能视为测试噪音。分阶段实现的本地检查和全部 PR CI 均已通过。只有 Core 支持现有页面属性且增量重绘整套等价测试通过后，才能切换默认。

**退出证据：**没有生产默认路径再选择回调度量/绘制；已安装 package 与 source override 的图像一致；应用字体和损坏资源拒绝测试通过。

### C. Core 自有文本 API 收敛

待 B 的消费者转用 Core 持有的字体目录后，让 Core layout、intrinsic sizing、layer 构建、repaint/reuse、rasterizer 和 Canvas2D 直接使用 `TextSystem`/拥有所有权的文本服务。移除依赖回调身份的缓存契约，并迁移回调测试、fixtures、source archive 测试和移植文档。PR #55 已单独移除 `HostTextAdapter`：全仓清单没有找到它的生产调用方。这不代表底层 `TextMeasureProvider`、`TextPainter`、基于 provider 的字体度量/范围 hooks 或 fallback 实现也已退役；这些剩余接口须等所有消费者迁移后，再通过协调的首次正式 ABI4 release 前 API 变更移除。

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
4. **主机集成已完成，WS147 验收等待回归定位：**PR #76（桌面）、#77（WS147）、Core #56、Runtime #79 与 #80 均已合并且 CI 通过。此前 staged ABI3 CoreText 镜像已启动并报告首帧 present；最近一次“合并主线固件”观察反馈切页后文字消失、首页时钟模糊/裁切，但尚未关联精确固件身份和 profile。复现并通过实机验收前，WS147 正常 Kconfig 默认值保持不变；更广泛的支持属性覆盖通过前，桌面 Core 文本继续显式 opt-in。
5. B 完成后继续 C 的回调消费者清单；所有消费者迁移前不移除 provider API。ABI4 签名发布、Runtime 生产锁和设备采用继续受 B、C、D gate 控制。
