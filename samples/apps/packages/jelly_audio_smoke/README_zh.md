# Audio Smoke

用于验证 Win32 host-owned audio 路径的小型 package。

这个 app 携带一个 1 秒左右的 PCM WAV 包内音频资源。它不声明 MCU MP3 capability。页面按钮使用
接近 Web 标准形状的 V0 子集：`new Audio("/audio/tone.wav").play()`，Win32 壳会把它映射到桌面
宿主音频 adapter。

如果只想验证 host handoff，不打开交互窗口，仍可用命令行 smoke 路径：

```powershell
.\build\Release\jellyframe_win32_browser.exe `
  --app samples\apps\packages\jelly_audio_smoke `
  --audio-smoke /audio/tone.wav `
  --audio-smoke-ms 1000
```

这只验证桌面壳边界。`tone.wav` 不是 ESP32-S3 MP3 pipeline 的验收资源；产品级 audio
codec、I2S 和播放 task 仍归宿主/移植层实现。
