# App 作者环境

> 最后更新：2026-08-28；适用版本：0.6.0-dev

JellyFrame 有两类使用者，安装方式也应当不同：

- **App 作者**只需要 VS Code 扩展、已安装的 JellyFrame SDK 和 Device OS provider；不应把
  App 放进 JellyFrame 源码 checkout，也不需要安装 ESP-IDF、JerryScript 或构建整个仓库。
- **框架/移植维护者**才需要完整源码、Render Core、Runtime、ports、硬件工具链和全部过程文档。

## SDK 与 App 工作区

App 是独立项目，根目录至少包含：

```text
my-app/
  jellyframe.app.json
  index.html
  styles/app.css
  scripts/app.js
```

SDK 是提供 CLI、target preset、schema 以及可用桌面运行时的安装目录。开发者第一次在
独立 App 工作区使用扩展时，执行 **JellyFrame：配置作者环境**，选择 SDK 文件夹。扩展会
保存一次机器级配置；之后打开其他 App 项目无需再次设置 `repoRoot`。若项目需要固定使用
某个 SDK，也可以在 `.jellyframe/project.json` 中记录：

```json
{
  "format": "jellyframe.app.project",
  "formatVersion": 1,
  "sdkRoot": "C:/JellyFrame/sdk"
}
```

`sdkRoot` 可以是绝对路径或相对 App 根目录的路径。扩展也识别环境变量
`JELLYFRAME_SDK_ROOT`。SDK 必须包含 `tools/jellyframe_cli.py`。打包作者 SDK 通常携带桌面
运行时；完整源码 checkout 可以本机构建。两者都没有的精简 SDK 会被明确标记为不完整，
不会尝试一个必然失败的 CMake 构建。

扩展的报告、截图、frame script 和临时资源默认写入 App 自己的 `.jellyframe/build/`，
不会污染 SDK 或框架源码。`jellyframe.buildDir` 仍可用于明确指定共享或 CI 输出目录。

## Schema 与模板

扩展 VSIX 自带 App manifest schema，独立工作区不依赖仓库相对路径。官方模板的
`$schema` 指向可访问的 GitHub raw 地址；编辑器安装扩展后使用本地随附 schema，离线也能
获得校验。不要使用不存在的 `jellyframe.dev` schema 地址，也不要把源码目录下的
`../../../../tools/schemas/...` 路径复制到新项目。

通过活动栏的 **从模板新建 App** 创建项目，选择一个不在 SDK checkout 内的目录。blank
模板只包含 Hello world、一个 CSS 注释、一个 JavaScript 注释和最小 manifest，适合先验证
环境。之后可以直接使用检查、预览、桌面调试、打包和设备部署。

## 产品分发边界

VS Code 扩展是 App 作者入口，不是完整框架源码的替代品。正式分发的 SDK 应提供与扩展
版本匹配的 CLI、schema、target preset 和预构建桌面壳；Device OS provider 作为独立的
版本化安装包提供。源码仓库继续服务于 Runtime/Core/port 维护者。扩展不会在用户不知情时
下载源码、修改项目或猜测设备端口；环境缺失时显示可执行的配置动作和具体路径要求。

维护者使用 `project_tools/package_app_author_sdk.py` 从验证过的标准桌面 Release 生成 SDK ZIP；
若要调试带脚本的 App，同时传入 scripting Release。SDK manifest 记录每个文件的 SHA-256 和
实际包含的 desktop profile。发布 SDK 前必须运行解包后的 CLI/template 冒烟测试，不能把本地
源码 checkout 或未验证的 build 目录直接作为作者下载包。
