# 桌面调试工具

> 最后更新：2026-08-10；适用版本：0.6.0-dev；兼容基线：0.5.0

这里是 App 作者使用桌面验收工具的统一入口。当前唯一支持的桌面可执行文件是
`jellyframe_desktop_shell`。

```powershell
python tools\debug\jellyframe_debug.py --list-builds
python tools\debug\jellyframe_debug.py --app samples\apps\packages\watch_weather
python tools\debug\jellyframe_debug.py --app samples\apps\packages\watch_weather --capture build\watch_weather.bmp --wait
python tools\debug\jellyframe_debug.py --app tests\fixtures\apps\jelly_scroll_probe --frame-script tests\fixtures\apps\jelly_scroll_probe\capture_wheel_scroll.jfcapture --wait
python tools\debug\jellyframe_debug.py --build-dir build\Debug -- --help
```

该入口只负责启动桌面壳，不实现第二套渲染器，也不代表 MCU 帧率、面板/DMA
行为或实机字体效果。相关结论必须使用移植侧验收流程。
