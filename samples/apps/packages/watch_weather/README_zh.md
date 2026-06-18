# Watch Weather 示例

一个紧凑的手表天气 source package，用于验证 package 结构、本地资源、事件委托、小屏 grid
布局，以及 Win32/pseudo-browser 预览路径的一致性。

该示例保持 local-first。未来如果接入真实天气数据，应通过 package capability 声明后的宿主
API 进入 runtime，而不是让 renderer 直接加载远程页面。
