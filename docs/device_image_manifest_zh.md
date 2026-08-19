# Developer Image Manifest V0

> 最后更新：2026-08-19；适用版本：0.6.0-dev；状态：契约基线

每个可发布的第一方 Developer Image 都必须在 firmware 与 recovery
materials 旁发布一份不可变 JSON manifest。它是 Device OS release tooling 与未来
`jellyframe-device` provider 的共同 identity record；它不是 JFDP message、App
manifest、board driver configuration 或安装命令。

规范 schema 位于
[`tools/schemas/jellyframe.device_image.schema.json`](../tools/schemas/jellyframe.device_image.schema.json)。
`tools/device_image_manifest.py` 提供 Runtime 侧严格 parser 与 provider-device
compatibility check；两者都会拒绝 unknown field 与 duplicate JSON member。

## 必填 Identity

```json
{
  "format": "jellyframe.device-image",
  "formatVersion": 0,
  "imageId": "org.jellyframe.ws147.developer",
  "imageVersion": "0.1.0-dev",
  "runtimeVersion": "0.6.0-dev",
  "renderCore": { "version": "0.6.1", "abi": 1 },
  "source": {
    "revision": "<40-lowercase-hex>",
    "firmwareSha256": "<64-lowercase-hex>"
  },
  "board": {
    "id": "ws147",
    "display": { "width": 172, "height": 320, "shape": "rect" }
  },
  "profile": {
    "id": "rect-172x320",
    "featureFamilies": ["core.document"]
  },
  "transport": { "protocol": "JFDP/1", "kind": "usb-serial-jtag" },
  "storage": { "maxBundleBytes": 327680 },
  "recovery": {
    "procedureId": "ws147-usb-recovery-v1",
    "factoryImageSha256": "<64-lowercase-hex>"
  }
}
```

`firmwareSha256` 标识精确测试/发布 image。recovery hash 标识由 `procedureId`
命名的 factory image；它不是擦除或烧录设备的命令。实际 procedure 必须由版本化
Device OS release document 提供，写明 host tool、target board、confirmation step
和 recovery outcome。

## Provider Matching

CLI 或 editor 部署 App 前，必须先用 `device_provider_contract.py` 解析 provider
discovery result，再精确比较 board ID、profile ID、image version、Runtime version、
`JFDP/1`、display shape/dimension、max bundle bytes 和 feature family set。
`availableStorageBytes` 是动态状态，刻意不作为 image identity。

任何 mismatch 都是 provenance 或 compatibility failure。tooling 必须明确报告，
不得猜测 serial endpoint、修改 board manifest 或尝试 install。physical provider
仍属于 Device OS；该契约只在不把 USB、serial、board dependency 引入 JellyFrame
Runtime 的前提下提供稳定、可测试的输入。

## WS147 已发布基线

WS147 Developer Image manifest 与 factory procedure 已发布并完成实机验证。2026-08-19 基线为
`org.jellyframe.ws147.developer@0.1.0-dev`，source revision
`fbf10784ac8ce38f41ced40fa013a43564c992c8`，Runtime `0.6.0-dev`、Render Core
`0.6.1` / ABI `1`、profile `rect-172x320`。firmware SHA-256 为
`e7eb9b16cce5d9e781fc93717826192781b652704e91d1859f353f74e5cfaacc`；16 MB factory image SHA-256 为
`6f3360753422c60ba32a1cee92fd01575e64ae41433acc31b9fb35d527bad2e1`。

严格 manifest/provider 检查与 offset zero 的完整 factory 写入均已在 WS147 验证：recovery 以
`RegistryInvalid` 进入 protected launcher，随后无需重新烧录即可通过 JFDP 安装并启动 lifecycle fixture。
这只关闭 A1 的 manifest/factory-recovery 子项。未来 image revision 必须重新发布 manifest、hash 并做
compatibility assessment，不能复用此 image identity。
