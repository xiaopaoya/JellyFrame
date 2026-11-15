# Render Core ABI4 集成计划

> 最后更新：2026-11-15；适用版本：0.6.0-dev。Core 开发契约：0.9.0-dev / ABI4。

本文件是当前执行计划。旧检查点保存在 Git 历史和对应 PR 中，不再作为额外待办。
[文本消费者退役清单](render_core_text_consumer_retirement.md)记录实际调用方与原生设备候选的完整身份。

## 当前检查点

| 边界 | 当前状态 | 剩余门槛 |
| --- | --- | --- |
| 已发布依赖 | Runtime 生产锁仍为签名 Core v0.8.0 / ABI3。 | 最终 ABI4 契约发布后，更新精确依赖锁。 |
| Core 主线 | PR #61，`3152300a`，0.9.0-dev / ABI4；source identity `c11045754fa28b9bf3e5e0e9a8720dc202efd7dd9c66c159c058f5edee7dd69b`，163 个实现输入。 | 剩余消费者、属性迁移和回调退役。 |
| Runtime 主线 | PR #101，`8262edf7`，CI 19/19，run `43670532408`；#100 已迁移 embedded host。 | source override 资格验证不等于生产默认采用。 |
| Embedded host | #100 为 layout/layers/paint 绑定同一个 TextSystem/JFFONT2；锁定 ABI3 和 source ABI4 full/cropped 测试通过。 | 该示例不再依赖文本 provider 回调。 |
| Installed worker | 已合并 #101，让无 painter 的 canonical v10/CoreText 独立于 BMP 工作。 | 实机验收仍是下游门槛。 |
| 原生 WS147 | 精确候选 `0.6.2-abi4-mainline1` 已构建并核验包，尚未刷写。 | 在该产物上观察 Home/Activity/Weather/返回。 |
| ABI5 与 shaping | 独立可选候选；ABI5 tracked reuse 有正确性与包测试，尚无合格性能收益。 | 未启用到 Runtime，也不阻塞 ABI4 发布。 |

实验源码声明的是 0.6.2 / ABI9。版本号或合并 PR 数量都不能证明迁移完成；
每项能力还需要受治理的 Core 契约、Runtime 消费者、资源交付和适用测试。

## 并行工作线

### A. 资源交付与默认字体目录

已完成签名包离线 bootstrap（#73）、共享 Noto Sans SC JFFONT2 字体目录及所有权 API
（#75）、显式桌面/设备目录路径（#76/#77），以及后端/源码身份、路由和重绘验证
（#79–99）。Core #60 提供有界 letter-spacing 和 JFTP v3 显式位置，仍能解码 v1/v2。
规范路径由 Core 负责选择、度量、准备和栅格化，移植者只注册不可变字体资源。

剩余：迁移 generated C++ BitmapFont 默认资源，以及优先选择回调的 legacy JFFONT
应用字体路径。保持确定的 family/weight 选择、字体字节寿命、缺字拒绝和资源预算，
不增加新的宿主文本回调。

### B. Runtime 消费者迁移

| 消费者 | 进展 | 完成证据 |
| --- | --- | --- |
| Embedded host | #100 完成。 | 可见文字、click/checkbox/select、RGB565 flush；ABI3 与 ABI4 full/cropped。 |
| ESP32 installed worker | 已合并 #101：无 painter 的 canonical v10；目录/快照覆盖重放寿命，BMP 可选。 | 主机 full/cropped、锁定 ABI3、真实 IDF 构建、CI，随后精确设备 profile 验收。 |
| ESP32 retained native/default | 命名 CoreText profile 已合并，默认仍使用 generated BitmapFont provider。 | 迁移默认资源契约并验收所支持属性和页面路由。 |
| ESP32 非 retained main 入口 | 仍有独立的 provider 工厂。 | 同步迁移或明确退役该入口，不能只改 retained UI。 |
| Win32 默认路径 | 显式 CoreText 目录模式已合并，默认仍为 GDI。 | 有界 ellipsis/属性兼容、重绘验证及目录默认采用。 |
| AppFontSet | JFFONT2 由 Core 负责；legacy JFFONT 仍优先选择回调。 | 迁移早期应用字体格式/fixture，删除回调访问接口。 |

Canvas2D 已使用 TextSystem，不是剩余回调消费者。Installed worker 候选在写帧缓冲前
拒绝依赖回调的普通 `Text` 命令；这是有界准入规则，不代表旧路径支持的全部
生成内容或控件文字均已迁移。

已合并 #101 的 worker 主机证据：Core PR60 full 和关闭四类可选能力的 cropped 均通过 7/7；
BMP OFF/ON 都覆盖可见 prepared text、JavaScript 文字修改、退出后的重放和原子拒绝。
锁定的已安装 ABI3 包通过四项 admission 测试，两项 canonical 测试按预期跳过。
CoreText OFF 的 legacy 路径保持显式选择。

### C. Core 文本 API 收敛

Core #61 已在四项 CI 全部通过（run `43670844631`）后合并为 `3152300a`。有界 owner-side ellipsis 的 MSVC full 测试 33/33、已安装 full/cropped 文本消费者各 1/1 通过；cropped 唯一 shared-inline 阴影断言失败在未修改基线同样复现。当前 Runtime 资格验证更新固定该精确源码，并增加五帧真实字体增量/全量省略号测试和 clip-only 可见对照。更广属性/默认采用仍独立推进。该实现必须保留源文本布局/固有尺寸，在最终几何
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

该镜像上的 Home/Activity/Weather/返回、八行预报文本、清晰度和裁剪仍待实测。
最近的小卡片缺字反馈不能由桌面路由测试或身份不明的旧镜像关闭。
Installed worker/BMP 必须使用独立命名镜像和资源配置；构建通过或积极假设都不等于实机通过。

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

1. 完成 Runtime 对已合并 Core #61 的精确 full/cropped 包/源码资格验证，以及正向真实字体 ellipsis 重绘门槛。
2. 在默认采用前继续补齐属性/资源消费者；#101 worker 和 #61 有界 ellipsis 实现已合并，不再列为开放工作。
3. 迁移 native/default 与 AppFontSet 资源；转换剩余回调调用方和测试，集中删除旧 API。
4. 上述门槛通过后签名发布 ABI4，并切换 Runtime 精确依赖锁。
5. 用户方便时执行已预留的原生板测；设备问题单独跟踪，并在容量合格的 profile 上
   准备 installed 应用设备验收。
