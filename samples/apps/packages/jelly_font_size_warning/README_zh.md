# 规范应用字体样例

> Last updated: 2026-11-15; Applies to: 0.6.0-dev

`fonts/*.jffont2` 来自已授权的 Noto Sans SC 2.002 Regular，使用 Core
`tools/prepare_font_resource.py`、Pillow 12.3.0 和 fontTools 4.60.1 生成。
原始字体位于 `ports/esp32s3-idf/font/source/noto-sans-sc-2.002/NotoSansSC-Regular.otf`。
覆盖字符和生成报告保存在仅供开发使用的 `fonts/.source/`，不会打包；
`fonts/LICENSE.txt` 保留 SIL Open Font License。报告记录原始字体度量、字形 ID、
源文件和资源的 SHA-256。16px 栅格档支持 Core 标量缩放，包括 12px；
历史上仅支持整数倍缩放的尺寸警告已退役。

先生成字体，再打包或导入。旧 `.jffont` / JFFONT0-v0/v1 和 BDF 二进制字体生成已退役；
BDF 生成 C++ BitmapFont 头文件仍是独立产品。

字体资源包含字形不代表当前 TextSystem 接受对应字符。字体策略样例中
故意缺失的假名探针会明确拒绝，不能作为可见字形通过的证据；中文命名字体通过独立像素测试验证。
