# Runtime 0.6 更新前构建与行为验证

> 最后更新：2026-09-03；适用版本：0.6.0-dev；状态：本机集成候选证据

本文记录 Runtime 版本更新前在独立集成工作树中的可复核验证。它不替代
ESP32-S3 的实机报告，也不关闭 A2 的干净作者机或 panel/input 出口。

## 验证范围

所有配置均使用 Clang 22.1.8、Ninja 和 CMake 3.30.0-rc1，在同一份集成工作树执行。
非脚本配置使用完整测试和工具目标；脚本配置使用本机 JerryScript checkout 的实际头文件与
`MinSizeRel` 库，不把非脚本结果当作脚本结果。

| 配置 | 构建 | CTest | 结果 |
| --- | --- | ---: | --- |
| Debug，scripting off | 通过 | 18/18 | PASS |
| Release，scripting off | 通过 | 18/18 | PASS |
| Debug，scripting on，script-task on | 通过 | 20/20 | PASS |
| Release，scripting on，script-task on | 通过 | 20/20 | PASS |
| Debug，AddressSanitizer/UndefinedBehaviorSanitizer，scripting off | 通过 | 18/18 | PASS |

## 覆盖内容

- Render Core、App Runtime、Device Runtime Contracts 的单元回归和构建边界。
- locked Core package/source provenance、feature profile、link-map 和文档 freshness 检查。
- JFDP reference/provider parser、设备 CLI、package preflight、registry 和工具错误路径。
- scripting backend、worker session/generation、frame/input/service/fatal/teardown 及跨 session 拒绝。
- 非脚本与脚本开关的依赖隔离；脚本后端仍由 configure-time JerryScript 选择。

## 结论与限制

当前集成候选没有发现新的主机侧构建、CTest 或 Sanitizer 回归，可以进入 Runtime 更新前的审阅阶段。
这不是版本发布批准：仍需确认 Core lock 与 release artifact 的 provenance 证据、冻结版本/API 文档，
并等待 A2 的干净 VS Code 生命周期和真实已安装 App panel/input 证据。Sanitizer 本轮覆盖非脚本配置；
JerryScript 需要兼容的 Sanitizer 构建产物后再单独启用，不以缺失该环境为通过。
