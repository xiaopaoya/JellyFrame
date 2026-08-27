# JellyFrame 活动待办

> 最后更新：2026-08-25；适用版本：0.6.0-dev
>
> 本清单是 [路线图](roadmap_zh.md) 的近期执行队列，不记录已经关闭的验收、性能微实验或历史移植任务。

## 现在：更宽范围 A2 产品出口与 B1 边界维护

- [ ] 在干净作者机完成 WS147 VS Code 的只读 smoke：发现、身份读取和已安装 App 列表必须与 manifest/registry 一致。要求见 `../docs/ws147_provider_vscode_smoke_20260825_zh.md`；该项不执行安装或刷写。此前本机 candidate smoke 不可替代此项。
- [ ] 在同一干净作者机完成 VS Code 设备流程：`new -> check -> package -> deploy -> launch -> live log -> update -> rollback -> stop -> remove`。桌面与设备 session 必须保持独立，最终报告必须保留可定位的失败归属。
- [ ] 通过 provider 流程完成真实已安装 App 的 panel/input 验收。记录 App launch marker、触控/输入响应、panel/present 错误与恢复行为；provider lifecycle PASS 不等于视觉或输入证据。
- [ ] 将 B1 作为持续 release gate 维护。首个带签名 Core `v0.6.0` 是历史基线，Runtime 当前锁定 `v0.6.1`；以后每次 Core bump 必须下载或以其他方式认证已审阅的 release artifact、校验 archive SHA-256、更新精确 version/ABI/source lock，并通过 standalone、package-consumer 与 source-override tests。
- [ ] 执行 [0.6 工程维护审查计划](engineering_review_plan_20260819_zh.md)：先做 R0 package/profile/provenance，再做 R1 document/style、layout/dirty 与 renderer/text。只修复有明确语义或安全缺陷的接口，不做机械式改名。
- [ ] 维护 B2 脚本运行时边界：通用 host 与 worker 代码只能包含 `script_runtime.h`；引擎 headers、value 和发现逻辑必须留在选定后端内。未经过独立批准的 RFC 与对等证据前，不引入第二后端，也不改变对外开发者口径。

## 并行：A3 内测筹备

状态：**进行中，由外部协作方负责宣传与筹备对接。**

- [ ] 准备最小试用包：已发布 Developer Image/provider、VS Code 扩展安装说明、`blank` 模板起步流程、已知能力边界与支持渠道。
- [ ] 固定反馈归档格式：App `.jfapp` 或源码包、image/provider/extension 版本、复现步骤、JellyFrame Output、设备 logs 与是否可复现的最小 capture。
- [ ] 准备首轮筛选与响应规则：安装、运行、恢复、数据损坏和文档化能力不符为 P0；不把未声明 Canvas、全屏 30 FPS 或完整浏览器 API 作为缺陷承诺。

这些筹备项不放行实际外部试用。只有上方两项 A2 正式证据和 panel/input 验收关闭后，才可分发访问与收集产品可用性数据。

## Core 发布后的候选能力

新的 Render Core 能力仅在独立治理的 Core release line，或已批准的同一拆仓 release window 内开始。每个候选都需要可复现的作者需求、RFC、正/负行为测试、三 target desktop capture、能力矩阵/诊断/recipe 更新和热路径 benchmark。

- [ ] 在独立 Core line 完成 `text-wrap: balance` 的 candidate evidence。完成前不得写入 Runtime 作者能力矩阵；之后还需由 Runtime 明确选择 package/default-provider integration。
- [ ] 只有在可复现作者需求与 RFC 明确 feature、profile impact 和 hardware budget 后，才能选择下一个 Core candidate；不得默认重新开启广泛 CSS 兼容性工作。

核心侧只在需要新增平台无关 contract 时介入；不得以 reference endpoint 伪造实机完成。

## 明确不进入当前队列

- [ ] 不继续 full-frame rounded/gradient 的 copy、span、DMA 等微优化；只有真实 developer-image workload 的 telemetry 能重新开启性能项。
- [ ] 不启用 retained replay、framebuffer reuse 或 tile/scanline renderer，除非先满足路线图中的独立证据门槛。
- [ ] 不将 Canvas、完整 SVG/video、Shadow DOM、Worker、iframe、`:has()` 或容器查询作为 `0.6` 默认范围。
- [ ] 不开始外部硬件开发者试用，直至更宽范围 A2 的干净机器与 panel/input 出口通过。

## 每项最低检查

- [ ] `git diff --check` 和相应 Debug/Release CTest。
- [ ] scripting、工具、package 或 profile 改动的定向回归。
- [ ] 热路径改动的 focused benchmark；硬件结论的版本化 port 报告。
