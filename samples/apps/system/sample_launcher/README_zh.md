# 示例启动器

> 最后更新：2026-07-14；适用版本：0.5.0

这是 Win32 宿主用于 bring-up、CI 和 App Manager 手动测试的 JellyFrame app
启动器样例。它是一个带系统权限的示例 app，不代表 JellyFrame 必须绑定固定的一方启动器。

当前 Win32 宿主会把已安装 app 列表注入到 `<!-- JELLYFRAME_APP_LIST -->`，
把状态文本注入到 `<!-- JELLYFRAME_STATUS -->`。未来可以用系统 API 替换这层
模板桥接，而不改变渲染管线。

桌面 registry mock 现在提供 V0 app-manager 状态：已安装 app 带有 `status`、
`enabled`、更新时间和可选 rollback 元数据。紧凑的设备库布局会分开显示 store 状态、可启动性和恢复状态，
将主操作 Open 与低强调的维护操作区分开，并提供宿主拥有的启动、启用/停用、回滚、清数据、删除并清数据、
删除但保留数据操作。破坏性数据操作必须经过独立的宿主确认步骤，确认前不会修改 registry 或 app 数据。
Win32 壳为 bring-up 保留原始本地 bundle 的
`--install-bundle`，并为已验证路径提供宿主生成的 `--install-candidate candidate.json`：候选文件必须记录
本地 bundle 的 SHA-256、`trusted` 签名结论和明确的用户批准，才允许提交安装。下载、TLS、签名验证、权限
提示和持久启动器 UX 仍完全由宿主负责，普通 app 不获得安装 API。
每次新安装前，宿主还会清理未完成事务遗留的 staging 文件，以及未被当前或 rollback registry
引用的 bundle；该恢复路径由宿主拥有，绝不删除 app 私有数据。
