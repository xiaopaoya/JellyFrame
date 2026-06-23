# Target Presets

这些文件是 packaging 和 validation tools 使用的设备能力 preset。

请保持 preset 保守。一个 preset 应描述 app 可以稳定依赖的能力，而不是某块开发板暴露的全部
可选硬件特性。

`jellyframe_cli.py check`、`preview`、`package` 和 `install` 可以通过显式传入
`--targets` 或 `--all-targets` 运行多 target responsive validation。该流程会对同一个
package 按每个 preset 跑一次 render-core pseudo browser，并把紧凑结果写入 JSON report 的
`responsiveProfiles[]`。

Responsive profile 是桌面开发信号，不是运行时功能。它报告 viewport 尺寸、形状、content
height、横向溢出、是否需要滚动以及 diagnostics 计数，帮助 app 作者判断一个 package 在多个
可穿戴设备形态上是否仍然可用。pseudo-browser report 还会在原始 layout bounds 旁输出
`paintBounds`；横向溢出按最终可见 display-list bounds 判断，避免被裁剪的实现盒子造成误报。
普通单 target 命令保持旧 report 结构，不输出 `responsiveProfiles[]`。

字体预算字段（`maxAppFonts`、`maxAppFontBytes`、`maxAppFontGlyphs`）是 installable
`.jffont` 补充包的工具限制。它们应反映 flash/storage 和安装策略预期，而不是每个 app 都共享的
系统固件字体。

可选宿主服务可以用 `hostServices` 描述：

```json
{
  "hostServices": {
    "networkFetch": true,
    "storageKv": true,
    "audioPlayback": false
  }
}
```

这会进入 package report 的 `serviceIntent.targetSupport`，取值为
`supported` / `unsupported` / `unknown`。它只是开发期兼容性信号，不是 app 权限授予。
当 app 请求了所选 preset 明确标记为 `false` 的服务时，package 还会输出
`service-target-unsupported` warning。缺失字段保持 `unknown`，不直接失败，因为产品 port
可能会在通用 preset 之外定义可选服务。

当前内置可穿戴 preset 把有界 runtime network fetch 和 app 私有 KV storage 标为 supported，
把 audio playback 保守标为 unsupported。具有真实 codec/扬声器路径的产品 port 应在自己的
preset 中覆盖 `audioPlayback`，不要依赖通用显示形态 preset。

当前通用显示形态包括 `round-300`、`rect-320x240` 和 `rect-172x320`；
`esp32s3-round-300` 额外提供面向 ESP32-S3/RGB565 的 profile。`rect-172x320`
面向微雪 ESP32-S3-Touch-LCD-1.47 这类窄竖屏可穿戴面板。
