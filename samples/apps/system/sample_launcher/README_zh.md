# 示例启动器

> 最后更新：2026-07-07；适用版本：0.5.0-dev

这是 Win32 宿主用于 bring-up、CI 和 App Manager 手动测试的 JellyFrame app
启动器样例。它是一个带系统权限的示例 app，不代表 JellyFrame 必须绑定固定的一方启动器。

当前 Win32 宿主会把已安装 app 列表注入到 `<!-- JELLYFRAME_APP_LIST -->`，
把状态文本注入到 `<!-- JELLYFRAME_STATUS -->`。未来可以用系统 API 替换这层
模板桥接，而不改变渲染管线。

桌面 registry mock 现在提供 V0 app-manager 状态：已安装 app 带有 `status`、
`enabled`、更新时间和可选 rollback 元数据。Win32 壳可通过 `--install-bundle`、
`--remove-app`、`--delete-app-data`、`--rollback-app`、`--enable-app` 和
`--disable-app` 验证安装、删除、清数据、回滚和启用状态切换。这个示例只负责渲染这些记录并切换
enabled 状态，也会显示 failure reason；真实产品仍需自己实现下载、签名校验、权限提示和持久启动器 UX。
