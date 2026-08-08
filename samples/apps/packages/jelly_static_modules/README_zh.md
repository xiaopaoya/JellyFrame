# 静态模块

> 最后更新：2026-07-10；适用版本：0.5.0

这个小包证明打包期 ES-module 子集：`index.html` 有一个外部 `type="module"` 入口，
`scripts/app.js` import 一个 package-local helper。打包会把入口改写成生成的 classic script
bundle，因此设备 runtime 无需 module loader。

```powershell
python tools\jellyframe_cli.py preview --root samples\apps\packages\jelly_static_modules --output build\static_modules.bmp --build-dir build\Release
```
