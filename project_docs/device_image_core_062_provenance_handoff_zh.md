# Device Image Core 0.6.2 Provenance 交接要求

> 最后更新：2026-09-07；适用版本：0.6.0-dev

本文适用于 JellyFrame Runtime `0.6.0-dev` 线路及其锁定的 Render Core `0.6.2` 依赖。

本文记录使用 Render Core `0.6.2` 的 WS147 Developer Image 已验收证据，不提升或修改历史镜像身份。

Runtime `master` 当前消费带签名的 Core `v0.6.2`、ABI `1`、source identity
`539a894519d3251f02c8b3aee8d0d0fb715bf49a732fc74126ccb2188462e3f0`，archive SHA-256
为 `d136a0d7fd7ab58436a5f2fa9c7eb27a497e08bad384a96cd93689ba6898f43e`。

已发布 WS147 基线仍是使用 Core `0.6.1` 的不可变历史产物。新的候选镜像 `0.6.2-ws147.1` 已基于 Device OS revision `131ce8c15702eea6fff3187c10a0926ef21cfc98` 构建：firmware SHA-256 为 `9a67aef07b833fe7f6be8ace4ce70a23eed58df33bb3cda4642d4c022a2ebb72`，factory recovery SHA-256 为 `7256568c5741d4131d526a25e4072eda49c68778b70746efdc99c86a29eb427e`，manifest SHA-256 为 `c118df34a7f98eee3efb0b1b711b78b9d69ff614ce1f2acb390a9d11447ef031`，完整归档 SHA-256 为 `687d57903e8c1565c966cf3c7c4f9eaf8ef2e5fecdf29a4e1ac28ae4ab8839b1`。这些 identity 必须与候选归档绑定，历史 manifest 与证据不得原地修改。

## 候选状态

候选已通过有界入口/重连、rollback 完整性、已安装脚本 App 输入（20 次）、非脚本生命周期、显式更新/rollback/remove、确认中的取消、chunk/commit 掉电恢复、malformed/CRC/oversize/storage-full 矩阵、registry corruption 后 protected launcher recovery 和混合生命周期（30 次）证据。完整 Developer Image gate 为 **PASS**。受控 storage-refusal 与 load-failure 镜像仅作为验收 fixture，产品镜像未启用这些 fault point。

已验收镜像基于包含 `0.6.2` lock 的已合并 Runtime 提交和选定 WS147 profile 构建，并交付了：同一源码/配置生成的固件与 factory recovery image；新的 image version、source revision 与 hash；声明 Runtime `0.6.0-dev`、Core `0.6.2`、ABI `1` 及实际 profile 的 manifest；从精确镜像生成的 Provider identity；以及包含上述信息的 release record。

验收必须证明 generated provenance 为 Core `0.6.2` / ABI `1` / 预期 source identity；standalone、package-consumer、source-override Runtime 测试通过；manifest/provider 在安装前拒绝版本、ABI、profile、display、容量或 feature 不匹配；干净 WS147 完成刷写、重连、发现、身份和 App 列表；脚本与非脚本 App 完成 install、launch、stop、update、rollback、remove、取消和重启恢复；并重新提供 panel/input、frame/present 证据。既有 Core `0.6.1` 镜像证据不能关闭该门槛。

移植侧应保留完整报告及精确 artifact hash 作为 release record；Provider 交付 metadata 现在可以将该候选标识为已验收的 `0.6.2-ws147.1` Developer Image。历史镜像仍只能按 `0.6.1` identity 接受。详细执行顺序见英文交接文档。
