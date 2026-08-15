# Render Core 发布与拆仓政策

> 最后更新：2026-08-16；适用版本：0.6.0-dev

本政策规定当前 monorepo 边界迁移到独立 `jellyframe-render-core` 工程的方式。它补充 [engine_architecture_zh.md](engine_architecture_zh.md)，不是面向 App 作者的兼容性承诺。

## 所有权

| 工程 | 负责 | 不负责 |
| --- | --- | --- |
| `jellyframe-render-core` | HTML/CSS/DOM、layout、paint、input semantics、feature family、Core profile schema | App 安装、JerryScript、文件系统、设备传输、板驱动、launcher policy |
| `jellyframe` | Japp 格式、App Runtime、JerryScript binding、桌面壳、作者工具 | 板卡镜像策略或 Render Core 私有实现头 |
| `jellyframe-device-os` | launcher、registry、JFDP transport、官方镜像、ports | Render Core 语义或 Runtime 私有实现 |

## 发布单元

一份 Render Core release 包含：

1. Core 源、公开头、CMake package export 和 standalone tests。
2. 版本化 feature registry/profile schema 与生成 capability profile。
3. source manifest 与 SHA-256 artifact checksum。
4. 带签名的 annotated release tag 和公开 release artifact。

tag 签名建立发布权威；source manifest/checksum 建立 artifact identity，两者不可互相替代。现有确定性 archive 是该发布单元的前身。归档会先将声明为文本的成员规范为 LF，再打包；不透明二进制成员保持原始字节，因此等价的 CRLF/LF checkout 会生成相同的 archive 字节和 checksum。

## 版本与兼容性

- Render Core 从独立 `0.6.0`、Core ABI `1` 开始发布。
- patch 仅修复行为、鲁棒性或性能，不能改变已文档化的 Core 公开契约或 profile schema。
- `1.0` 前若必须破坏 Core API、feature-profile 或渲染契约，使用新 minor line 并给出明确 release note；错误的预发布契约可以移除，不保留 alias。
- ABI 与 package version 独立；只有导出的 CMake/公开头二进制契约不兼容时才递增 ABI。
- JFDP 使用 `JFDP/<major>`；破坏 wire 兼容时升 major。

## Consumer Lock

JellyFrame Runtime 在 dependency lock 中记录精确的 Render Core version、ABI 与确定性的 source hash。已发布 release metadata 另行记录 release archive SHA-256：已安装的 CMake package 能提供 source manifest，却无法证明其原始 archive bytes。每一次依赖升级都是显式评审改动，必须运行：

1. installed Core package 上的 Runtime tests。
2. 同一 Core revision 的 local source override Runtime tests。
3. published source artifact 的 Core standalone build/install/test。

Device OS pin 一个 JellyFrame Runtime release 与命名的 board feature profile；不得从 branch 名推断支持能力，并必须在每份 image 验收报告中记录 Core provenance。

lock 回滚也是显式 dependency change：恢复此前已验证的精确 version、ABI 和 source hash，随后重新运行 package-consumer tests。consumer 不得静默选择看似兼容的新版 Core package，也不得用 floating branch 充当回滚机制。

`JELLYFRAME_RENDER_CORE_SOURCE_DIR` 保留为互斥的本地开发 override，不是公开部署依赖，也不替代 locked package test。

## Profile 与裁剪

Core release 是源码/package 基础设施，不是一份通用 native firmware。每个 Device OS image 在构建期选择 feature profile；未选择 family 不链接，生成 capability profile 才是作者可依赖的运行时契约。App manifest 在读取资源前协商 required/optional feature。

普通 `.jfapp` 只能携带经过验证的数据资源，绝不加载 native Core module、shared object 或任意 binary。未来若确有 firmware feature pack，也必须由 host 签名、版本化并随 Device OS image lifecycle 回滚，不能由 App 包注入。

## 保留历史的拆分

首个 Core 仓库使用可复现的 `git filter-repo` export，保留 `src/render_core`、Core CMake boundary、standalone tests 与 Core 专属文档的历史。Runtime 仓库保留完整产品历史，并以一笔明确的 package-consumer 提交替代 in-tree Core。禁止用无历史目录拷贝替代。

发布新仓库前必须验证：

1. 干净 clone 不依赖 Runtime、JerryScript、ports 或 sample app 即可 build/test/install Core。
2. 干净 Runtime clone 通过 lock 消费已发布 Core package。
3. 本地 Runtime checkout 可使用文档化 source override。
4. Device OS profile build 记录精确 Runtime/Core provenance。

日常开发不使用 Git submodule；lock 加已发布 package 才是发布依赖机制。

## 演练

首次建仓前，在可丢弃目录中对 committed HEAD 运行演练：

```powershell
python project_tools\rehearse_render_core_history_export.py `
  --output-dir build\render-core-history-export
```

该工具需要 `git-filter-repo`，不会修改源 checkout；若导出丢失 Render Core 历史、缺少 standalone
入口，或仍包含 Runtime/port 路径则会拒绝。CI 会安装该工具，再对过滤后的导出执行 configure、build、CTest 与 install。演练只证明拆仓准备度，
不是实际的带签名仓库发布。

## 拆仓门槛

四条验证路径在一个 release candidate 上全部绿色，且不存在 Runtime/port 私有 include 反向进入 Core 后，才可物理拆分。第一个高价值 Core 能力包必须在受治理边界上开发，紧随拆仓或与其同一发布窗口完成；大量新 CSS 工作不得继续堆积在过渡 monorepo。
