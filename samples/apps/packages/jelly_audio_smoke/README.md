# Audio Smoke

Small package used to validate the Win32 host-owned audio path.

The app carries a roughly one-second PCM WAV package audio resource. It does
not declare the MCU MP3 capability. Its button uses the standards-shaped V0
subset `new Audio("/audio/tone.wav").play()`, which the Win32 shell maps to the
desktop host audio adapter.

The command-line smoke path is still available when you want to test only the
host handoff without opening the interactive shell:

```powershell
.\build\Release\jellyframe_win32_browser.exe `
  --app samples\apps\packages\jelly_audio_smoke `
  --audio-smoke /audio/tone.wav `
  --audio-smoke-ms 1000
```

This validates the desktop shell boundary only. `tone.wav` is not an ESP32-S3
MP3-pipeline acceptance asset. Product audio codecs, I2S and playback tasks
remain host/port responsibilities.
