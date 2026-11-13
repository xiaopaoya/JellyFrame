# Render Core ABI4 集成计划

> 最后更新：2026-11-14；适用版本：0.6.0-dev。范围：将经审查的实验能力纳入受治理的 Core/Runtime 路径，并退役由宿主负责文本度量与绘制的兼容接口。

## 当前检查点

- Runtime PR #71 已合并为 `50170381`，在 full/cropped 已安装消费者矩阵中资格验证 Core `b0bfd834` / ABI 4。合并后的 Runtime workflow [43462107068](https://github.com/xiaopaoya/JellyFrame/actions/runs/43462107068) 已通过。
- Core PR #54 已合并为 `de9733d2`，修复了有限范围的离线 shaping 原型，并记录确定性数据编码实验。合并后的 Core workflow [43462040656](https://github.com/xiaopaoya/JellyFrame-Render-Core/actions/runs/43462040656) 已通过。它没有增加 Core package 实现输入，也没有增加运行时 shaping 能力。
- Core PR #55 已合并为 `64c1cb42`：移除了没有生产调用方的 `HostTextAdapter` wrapper/header，但保留仍有生产消费者的 provider 接口。Core CI 已通过。随后 Runtime workflow [43472234209](https://github.com/xiaopaoya/JellyFrame/actions/runs/43472234209) 的 ABI4 full 与 cropped lane 均明确检出 `64c1cb42`；两套 installed-consumer 测试通过，并报告 163 个实现输入对应的 source identity `213ff3196864f2d7da9152bf246c29bda9849da50daff272baf551a7f9179cc5`。此前 `b0bfd834` identity 仅是清理前基线。
- Runtime PR #72 已合并为 `5c45c65a`；新增中英双语任务表，区分 ABI4 资格验证、生产采用、回调退役与设备验收。CI 已通过。
- Runtime PR #73 已合并为 `2c253566`；为带签名的 Core `0.8.0` / ABI3 archive 增加 checksum 校验的离线 bootstrap。Runtime CI 已通过；PR 前本地 67 项 CTest 也全部通过。bootstrap 让当前生产锁可复现，但不会切换到 ABI4，也没有建立默认字体目录。
- Runtime 生产依赖仍锁定带签名的 Core `0.8.0` / ABI 3。ABI4 尚无签名发布，也不是生产依赖。
- Core 完整字体资源路径仍为 opt-in。ESP32 原生 UI 和已安装应用 worker 仍保留生产回调/v6 路径；桌面 GDI/AppFontSet 兼容路径也仍存在。`TextMeasureProvider` 与 `TextPainter` 仍是 Core 公共 API，因为生产消费者仍在使用；目前只移除了没有生产调用方的 `HostTextAdapter` 便利层。
- 当前没有 ABI4 命名设备 profile 的 provenance 或 ABI4 固件验收记录。这是主机集成后的设备准入门槛，桌面 CI 不能代替它。

## 可并行工作流

### A. 资源交付与字体目录默认路径

Runtime PR #73 已建立针对当前签名 Core package 的干净 checkout、checksum 校验 bootstrap。剩余工作是为桌面与 WS147 确定一套经审查的默认字体资源。明确许可证、字形覆盖、字重/字号、存储所有权、内存限制、应用字体补充方式和确定性拒绝行为。Runtime 持有应用资源；Core 负责字体选择、度量、shaping 和字形准备。

**退出证据：**全新 checkout 不依赖用户本地文件即可构建；full/cropped/禁用特性 profile 通过；package identity 与生成 provenance 一致；资源及内存预算已记录并有测试。

### B. Runtime 消费者迁移（资源契约固定后并行）

- **B1 原生 UI 与已安装应用 worker：**ESP32 原生 UI 从旧 `AppFontContext` 回调迁移；常规 worker 从 frame v6 转为绑定字体目录的 v7。在改变正常 Kconfig 默认值前，先验证 full/cropped/disabled 主机构建中的 worker session、accepted-frame 所有权、脚本更新和原生 UI 绘制。
- **B2 桌面：**让 Core 字体资源成为无参数启动时的默认文本路径；把应用字体补充迁移至 Core 字体目录；只有在 capture、交互和失败行为均与已验收 Core 路径一致后，才移除 GDI/AppFontSet 选择。

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
A1 bootstrap 已完成；A2 字体目录/资源契约待完成
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
3. 完成 A 的字体目录/资源契约。当前 Core 自有 Noto 资产只有 16 px Regular；它还不是桌面/WS147 共用默认字体，也没有实机验收证据。
4. 固定资源契约后拆分 B1/B2；并行继续 C 的回调消费者清单，等消费者迁移后再移除 provider API。
5. ABI4 签名发布、Runtime 生产锁和设备 profile 继续受 B、C、D gate 控制；资格验证和 source bootstrap 不等于生产采用。
