# JellyFrame 可视化 App 编辑器计划

> 最后更新：2026-08-30；适用版本：0.6.0-dev；状态：阶段 3 完成，阶段 4 进行中；产品工作名：JellyFrame Visual Editor

## 产品定位

目标不是在 VS Code 中复制一个完整浏览器网页搭建器，而是提供一个 **JellyFrame 能力受限、源码可读、真实运行时可验证** 的 App 设计环境。它面向会写少量 HTML/CSS/JavaScript、但不应被要求从空白代码手工完成全部小屏布局的 App 作者。

最终工作流应是：

1. 从 blank 或 recipe 创建 App。
2. 在可视化编辑器中用组件和层级树完成结构、布局与基础样式。
3. 在需要时直接编辑生成的 HTML/CSS 和作者维护的 JavaScript。
4. 在同一编辑体验中切换到真实 JellyFrame 桌面壳验证。
5. 通过静态检查、程控回放或设备部署继续现有生命周期。

编辑器不是源码的替代品。HTML/CSS、设计模型与 JavaScript 都属于 App；无云端私有格式，也不要求运行时解释编辑器 schema。

## 借鉴与取舍

| 项目 | 采用的思路 | 不采用的部分 |
| --- | --- | --- |
| GrapesJS | Component Model 是最终输出的真相；Canvas 只是 View；Block、Trait、Layer、Command、Storage 各自有边界 | 任意 HTML/CSS 识别栈、完整浏览器 Style Manager、插件生态和远程资产模型 |
| Craft.js | 可序列化节点树、统一 node action、drop connector、history、独立 layers panel | React 组件作为最终 App 运行模型；JellyFrame 仍生成普通 HTML/CSS |
| Puck | 声明式组件配置、字段类型、slot、受控 viewport、可组合编辑器界面 | 面向 React 应用的自由组件执行和服务端数据模型 |
| Webstudio | Navigator 是复杂层级的主要操作面；画布顶栏管理 breakpoint/viewport；属性按结构分组 | 完整 CSS cascade/class/token 编辑器和浏览器级响应式自由度；其 AGPL 代码不进入本项目 |
| LowCodeEngine | “物料 + setter + schema + renderer”分层，最小内核与扩展协议的长期方向 | 当前阶段不引入企业级插件、远程物料市场、数据源编排或低代码协议栈 |

只借鉴公开交互与架构思想，不复制第三方实现。当前扩展继续使用第一方轻量实现，避免在几天内引入 React、GrapesJS 或大型构建链；这些框架的浏览器假设和输出自由度也与 JellyFrame 的有界设备模型不匹配。

## 最终期待效果

### 编辑器壳

- 顶部为单一命令栏：App 名称/保存状态、undo/redo、目标设备、Design/Runtime 模式、保存与运行。
- 左侧在 **组件** 与 **结构** 间切换。组件按 Layout、Content、Controls、Recipes 分类；结构树精确显示嵌套、顺序、隐藏/锁定状态和稳定 ID。
- 中央是可缩放设备画布，显示目标尺寸/形状、明确插入线、选中边框和 breadcrumbs；空容器有可发现的 drop target。
- 右侧按 **内容、布局、外观、交互** 分组，只提供 JellyFrame 真正支持的 setter。固定集合使用 segmented control、select、swatch、stepper 或 checkbox，不滥用自由文本。
- 左右面板可调整宽度；窄窗口能折叠，不出现文字、画布或操作重叠。
- 底部状态区显示当前节点、保存/冲突状态、能力诊断和最近一次真实渲染结果，不滚动堆积原始日志。

### 编辑行为

- 画布和 Navigator 双向选择；支持 before/after/inside 拖放、键盘移动、复制、删除、undo/redo。
- 组件的默认值、可放置位置、字段、验证、设计视图和 HTML serializer 由同一声明式 registry 定义。
- 稳定 ID 是 JavaScript 与 `.jfcapture` 的交互契约；修改 ID 时立即检查格式、重复和 listener/capture 风险。
- 常用长度采用数值 + `px/%/auto` setter；颜色采用 swatch + 合法文本回退；flex 对齐采用图形化选项。
- recipes 是可展开的普通节点子树，不引入黑盒组件或运行时依赖。

### 源码与真实渲染

- `.jellyframe/visual-editor.json` 是编辑模型；HTML/CSS 标记区是其可读投影。JavaScript 始终由作者维护。
- 首次接管必须确认并备份；以后保存记录生成区 digest。若作者在外部修改生成区，编辑器必须给出重新载入、查看差异或放弃覆盖，不能静默写回旧模型。
- 三个文件的写入使用一个 VS Code workspace edit；任一失败不得把模型和源码留在不同代际。
- Design 模式追求快速编排，但不声称像素权威。Runtime 模式复用现有桌面壳会话和帧协议；字体、圆角、动画、输入和 diagnostics 以真实 Render Core 为准。
- 静态 check 与真实 Runtime diagnostics 尽可能映射到 node ID 和属性组；无法可靠归因时显示源码位置，不猜测节点。

## 阶段计划

### 阶段 0：受限原型，已完成

范围：六类基础节点、value model、基本拖放、属性编辑、undo/redo、三种视窗、包内图片、源码生成、首次备份和“保存并调试”。

已通过：模型/路径回归、VSIX 内容检查、无头 Edge UI smoke，以及六类组件组合的 package warning `0` / Render Core diagnostic `0`。

阶段 0 只证明路线可行，不构成可宣传的成品 UI。

### 阶段 1：编辑器壳与结构操作，当前阶段

目标：让原型首先像一个稳定、清晰的开发工具，而不是三栏表单 demo。

- 建立组件/结构双视图和可折叠 Navigator。
- 统一 selection、insert-before/after/inside、keyboard action 和 history command。
- 重做顶栏、画布工具栏、节点标题、属性分组、空状态、focus/hover/error 状态。
- 增加面板 resizer、窄窗口折叠和 VS Code 深浅主题 fallback。
- 将 webview CSS/behavior 从宿主模板中拆出，宿主只负责文件、协议和生命周期。

出口：在 `1280x720`、`1440x900` 和窄编辑区均无重叠；40 节点、6 层嵌套可由 Navigator 可靠选择和重排；所有动作可 undo/redo；关闭未保存标签有明确提示；无 console error。

### 阶段 2：声明式属性与源码一致性

目标：消除继续扩展时的硬编码和数据损坏风险。

当前进度（2026-08-30）：第一片已完成。模型支持从 v1 迁移到 v2；组件 palette 使用宿主下发的可序列化 registry；保存后记录 HTML/CSS 生成区域摘要；外部保存会更新冲突提示，覆盖生成区域必须明确确认。三文件写入按确定顺序保存，任一文件保存失败都会尝试恢复全部原始内容；故障注入测试覆盖可恢复和回滚失败两种情况。属性面板现在由 registry 声明的内容、布局和外观字段驱动，Webview 与宿主模型验证器对数值、枚举、长度、文本、颜色和包内资源执行同一 typed setter 基线；registry 还提供经过验证的 renderer key，由设计画布和源码 serializer 共同消费，缺少 renderer 时会明确失败；运行时/源码 serializer 仍是权威渲染器。完整 renderer 能力对齐和扩展点仍未完成，阶段出口保持未关闭。

- 建立 component registry、typed setter 和 placement rule。
- 属性分为 Content/Layout/Appearance/Interaction，并统一长度、颜色、枚举与资源 setter。
- 增加 model migration、generated-region digest、外部变更监听、冲突 UI 和恢复测试。
- 写入失败时验证三文件代际一致；备份可从编辑器直接定位和恢复。

出口：新增一种简单组件不修改通用 inspector/render dispatcher；手改生成区不会被静默覆盖；故障注入不能产生半保存；非法值在进入 model 前被拒绝。

### 阶段 3：真实 Render Core 闭环

目标：编辑器内同时具备快速设计和权威运行结果，不形成第二套真相。

- Design/Runtime 模式共享 selection、viewport 和 App session。
- Runtime 模式复用现有 VS Code 桌面壳帧、输入、日志和 teardown，不新写 renderer。
- 保存后可增量触发 bounded check；diagnostic 映射到 node/属性或源码位置。
- Runtime 停止、继续、重启、标签关闭均沿用已经验收的进程生命周期。

出口：三个 target 的真实帧尺寸正确；动画和交互持续更新；关闭后无残留进程；Render Core warning/error 能在编辑器中定位；Design 视图差异不会被报告为真实像素结果。

### 阶段 4：少而精的物料与响应式设计

目标：不用手写布局即可做出有展示价值、仍然可维护的 JellyFrame App。

- 基础物料扩展到 stack/row/card/divider/spacer/text/image/button/input/select/progress/list/navigation 等有实际支持的节点。
- 提供 4 到 8 个可展开 recipe，例如状态卡、设置行、底部导航、圆屏信息组和表盘信息层。
- 增加包内资产浏览、最近颜色和经过验证的设计 token。
- 只对 manifest 已声明 target 提供响应式 override；界面明确显示继承与覆盖，不制造完整浏览器 cascade 假象。
- 交互面板先提供 stable ID、已有 listener/capture 状态和可复制事件骨架，不自动生成复杂业务逻辑。

出口：使用 blank 模板、可视化编辑器和少量手写 JS 能完成一个多状态演示 App；三个 target 通过 check；生成源码可读且无私有运行时组件。

### 阶段 5：Beta 扩展，受试用需求驱动

候选：多 route/page、组件 subtree 保存、第一方物料包协议、受限源码导入、data/service binding、协同诊断和自定义 setter。

这些项目必须由真实试用需求进入 RFC。第三方物料市场、任意 HTML 双向 round-trip、Figma 级自由画布、完整 CSS 面板和自动生成业务 JavaScript 不属于近期承诺。

## 阶段 3 进度

截至 2026-08-30，内嵌桌面壳会话已经复用正式的帧、输入、日志和 teardown bridge。第一片交接能力已完成：每次运行按 run ID 隔离 runtime log 与报告，桌面壳退出后自动调用现有 `check` 流程生成报告，并向视图明确显示报告生成状态；可视化编辑器“保存并运行”会将模型 viewport 传给同一内嵌会话，并由宿主执行边界校验，普通菜单调试仍使用 App 默认尺寸。编辑过程现在使用 180 ms 防抖的有界模型检查，只验证最多 128 个节点和生成区，不启动完整 CLI/Render Core 流程；报告中的管线诊断仅在包含稳定可视化节点 ID 时定位到 HTML 的 `id` 属性和属性组，否则明确标记为不可归因。剩余工作是集中回归和真实桌面交互验收。
截至 2026-08-30，内嵌桌面壳会话已经复用正式的帧、输入、日志和 teardown bridge。第一片交接能力已完成：每次运行按 run ID 隔离 runtime log 与报告，桌面壳退出后自动调用现有 `check` 流程生成报告，并向视图明确显示报告生成状态；可视化编辑器“保存并运行”会将模型 viewport 传给同一内嵌会话，并由宿主执行边界校验，普通菜单调试仍使用 App 默认尺寸；编辑器通过生命周期状态桥显示 `running`、`reporting` 和 `stopped`。编辑过程现在使用 180 ms 防抖的有界模型检查，只验证最多 128 个节点和生成区，不启动完整 CLI/Render Core 流程；报告中的管线诊断仅在包含稳定可视化节点 ID 时定位到 HTML 的 `id` 属性和属性组，否则明确标记为不可归因。剩余工作是集中回归和真实桌面交互验收。

## 阶段 4 当前进度

截至 2026-08-30，第一批物料已经接入 registry 和画布/源码两套 renderer：新增 `divider`、`spacer`、受限 `select`（1 到 6 个选项）、受限 `list`（1 到 8 个项目）和受限 `navigation`（2 到 4 个项目）。列表属性在右侧面板中使用增删项目控件，并在进入模型前统一检查数量、长度和安全文本。生成结果只是普通的 `div`、`select`、`ul`/`li`、`nav`/`button`，不引入私有运行时组件。`select` 仍受 App target 声明的 forms 能力约束，编辑器不宣称提供不受限的浏览器选择器。画布增加了包含历史、适应、缩放、结构和保存的简洁图标浮动工具栏。组件库还提供状态卡、设置行和底部导航三个透明模板组合，插入后会展开为带新稳定 ID 的普通可编辑节点子树；设置行使用受限开关，设备导向默认物料使用黑色或近黑色表面。属性面板会从包内脚本中显示可静态识别的稳定 ID 监听器，并可复制最小事件骨架，不会改写作者 JavaScript。target-specific override 仍未完成，阶段 4 尚未关闭。

## 近期展示门槛

内测宣传前不要求完成全部阶段。最小展示候选必须满足：

1. 阶段 1 出口全部通过。
2. 阶段 2 至少完成 generated-region 冲突保护与 typed setter 基线。
3. 阶段 3 至少保留可靠的“保存并实际调试”交接；不得只展示浏览器近似画布。
4. 用一个非仓库源码目录中的 blank App 录制 `create -> visual edit -> save -> runtime debug -> check -> device deploy`。
5. 宣传口径称其为受 JellyFrame 约束的 visual editor，不称完整 H5 low-code platform。

## 质量门槛

- 每阶段执行 `git diff --check`、Node syntax、model/file/UI regression、VSIX 内容检查。
- UI 需要 Edge/VS Code 深浅主题截图，覆盖 `1280x720`、`1440x900` 与窄编辑区。
- 所有生成节点组合至少跑 package + Render Core smoke；新增物料必须有正/负验证。
- 文件冲突、损坏 model、缺失 asset、重复 ID、节点上限和标签关闭必须有故障测试。
- 不以浏览器画布截图替代桌面壳 capture，不以桌面结果替代设备性能或输入验收。
