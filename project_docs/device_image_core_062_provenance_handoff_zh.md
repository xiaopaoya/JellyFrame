# Device Image Core 0.6.2 Provenance 交接要求

> 最后更新：2026-09-04；适用版本：0.6.0-dev

本文适用于 JellyFrame Runtime `0.6.0-dev` 线路及其锁定的 Render Core `0.6.2` 依赖。

本文定义 WS147 Developer Image 声明 Core `0.6.2` 前必须完成的证据。不提升或修改现有镜像身份。

Runtime `master` 当前消费带签名的 Core `v0.6.2`、ABI `1`、source identity
`539a894519d3251f02c8b3aee8d0d0fb715bf49a732fc74126ccb2188462e3f0`，archive SHA-256
为 `d136a0d7fd7ab58436a5f2fa9c7eb27a497e08bad384a96cd93689ba6898f43e`。

已发布 WS147 镜像仍是使用 Core `0.6.1` 的不可变历史产物。实际重新构建和验收前，历史 manifest、固件 hash、恢复 hash、Provider identity 和实机报告必须保持不变。

下一版镜像必须从包含 `0.6.2` lock 的已合并 Runtime 提交、按选定 WS147 profile 构建，并同时交付：同一源码/配置生成的固件与 factory recovery image；新的 image version、source revision 与 hash；声明 Runtime `0.6.0-dev`、Core `0.6.2`、ABI `1` 及实际 profile 的 manifest；从精确镜像生成的 Provider identity；以及包含上述信息的 release record。

验收必须证明 generated provenance 为 Core `0.6.2` / ABI `1` / 预期 source identity；standalone、package-consumer、source-override Runtime 测试通过；manifest/provider 在安装前拒绝版本、ABI、profile、display、容量或 feature 不匹配；干净 WS147 完成刷写、重连、发现、身份和 App 列表；脚本与非脚本 App 完成 install、launch、stop、update、rollback、remove、取消和重启恢复；并重新提供 panel/input、frame/present 证据。既有 Core `0.6.1` 镜像证据不能关闭该门槛。

在此之前，不能手工把历史 manifest 改成 `0.6.2`，工具只能按其 `0.6.1` identity 接受历史镜像。详细执行顺序见英文交接文档。
