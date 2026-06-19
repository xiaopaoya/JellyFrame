# 示例启动器

这是 Win32 宿主用于 bring-up、CI 和 App Manager 手动测试的 JellyFrame app
启动器样例。它是一个带系统权限的示例 app，不代表 JellyFrame 必须绑定固定的一方启动器。

当前 Win32 宿主会把已安装 app 列表注入到 `<!-- JELLYFRAME_APP_LIST -->`，
把状态文本注入到 `<!-- JELLYFRAME_STATUS -->`。未来可以用系统 API 替换这层
模板桥接，而不改变渲染管线。
