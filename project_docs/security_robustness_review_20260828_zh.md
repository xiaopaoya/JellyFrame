# 2026-08-28 安全与鲁棒性审查处置

> 最后更新：2026-08-29；适用版本：0.6.0-dev

本记录用于区分外部审查意见、已确认缺陷和暂不改变的架构建议。它不替代
Render Core、JellyFrame Runtime 或 Device OS 的测试报告；下文引用的 WS147 实机证据仅
覆盖所列安全与鲁棒性场景，不代表 A2 整体或作者工具流程已经完成验收。

## 已确认并修复

| 项目 | 处置 | 回归 |
| --- | --- | --- |
| 脚本 service 特权 kind、跨 session/input handle | JS gateway 仅接受显式允许的服务 kind；handle 必须属于当前 app/session/token | `script_task_service_bridge_tests` |
| 脚本预算但后端没有 watchdog | 初始化 fail closed，不再静默运行无 watchdog 的生产 worker | `jellyframe_script_task_runtime_tests` |
| surface 字节数溢出 | checked multiply，超过 `uint32_t` 或本机 size 上限即拒绝 | `app_services_tests` |
| CSS 百分比与 aspect-ratio 算术 | 使用 64 位中间值并对布局边界饱和，避免有符号溢出 | Render Core standalone tests |
| Provider 输出内存 | stdout/stderr 并发增量读取；任一流超过 256 KiB 即终止 provider | Python tool regression |
| `package --debug-dir` 删除源目录 | 拒绝源目录、源目录祖先和源目录内部路径；资源预算超限改为硬错误 | package preflight |
| history export `--force` 删除源仓库 | 拒绝包含源仓库的输出目录 | 工具静态路径审查 |
| WS147 入口资源固定 512 B | 启动不再用固定大小入口探针；完整 bundle 验证已确认 entry 存在，资源读取由 loader 决定 | `security-robustness-ws147-6cd26ab8-20260828` R1：768 B、3072 B 入口资源均启动通过 |
| WS147 rollback 记录未校验 | rollback 前重新校验 bundle、CRC、summary identity；失败时保持当前 active | `security-robustness-ws147-6cd26ab8-20260828` R2：损坏 rollback 被拒绝，active bundle 保持 |
| JFDP operation-result 计数关系 | C++ 与 Python codec 均拒绝 `receivedBytes > expectedBytes` | device contracts 与 Python regression |
| 参考设备掉电窗口 | 分片先 fsync；读取事务时丢弃 metadata 之后遗留的未提交尾部，使 resume/cancel 可继续 | Python device-reference regression |
| app 私有数据目录碰撞 | 私有目录采用 app ID UTF-8 的无损十六进制编码，不再依赖易碰撞的文件名归一化 | app registry regression |
| 触控任务释放竞态 | 释放 I2C/LCD 前等待 touch task 自行退出，不以 100 ms 超时继续释放依赖 | `security-robustness-ws147-6cd26ab8-20260828` R3：teardown 通过 |
| WS147 组合生命周期稳定性 | 组合安装、启动、恢复与资源边界场景保持确定性完成 | `security-robustness-ws147-6cd26ab8-20260828` R4：30/30 通过，无 panic、watchdog、reset 或外设错误 |

## 保留为后续 RFC 的项目

1. Render Core 的文件写出 API 仍由 `JELLYFRAME_ENABLE_IMAGE_FILE_IO` 控制。将它从
   Core 移到 desktop adapter 是合理的边界改进，但需要同时改造 Win32/pseudo-browser
   构建和 standalone package，不能只切换默认值制造构建失败；本轮不宣称已完成。
2. dirty/clip 公共指针接口和 renderer 的直接调用仍应增加显式 profile budget。当前
   value-frame codec 已通过调用方的 `max_commands`、`max_clips`、`max_input_targets`
   约束；下一步应为 Core API 设计统一上限和明确的超限结果，不用静默截断。
3. retained diff 的 command 比较为线性扫描，当前 frame command 数量受 codec/profile
   budget 限制；在启用 replay 前再评估哈希或结构索引，不能把当前 diff telemetry 当作
   framebuffer reuse 授权。

## 验证边界

本轮主线验证：Python 定向回归、Runtime/Render Core/Device Contracts Release 构建和
脚本 task runtime 测试通过。WS147 实机报告
`security-robustness-ws147-6cd26ab8-20260828` 已覆盖入口加载、corrupt rollback、触控
teardown 和组合生命周期；这些项目不再是本次审查的待验证项。A2 的干净作者机 VS Code
完整流程和真实已安装 App 的 panel/input 证据仍是独立出口，不能由本报告替代。
