# Audio Smoke

> Last updated: 2026-07-07; Applies to: 0.5.0

Small package used to validate the Win32 host-owned audio path.

The app carries a roughly one-second PCM WAV package audio resource and declares
the generic `media.audio.playback` capability. Its button uses the standards-shaped V0
subset `new Audio("/audio/tone.wav").play()`, which the Win32 shell maps to the
desktop host audio adapter. The sample also installs `onended` and `onerror`
handlers so the shell can validate status events without exposing a private
audio API.

The command-line smoke path is still available when you want to test only the
host handoff without opening the interactive shell:

```powershell
.\build\Release\jellyframe_desktop_shell.exe `
  --app samples\apps\packages\jelly_audio_smoke `
  --audio-smoke /audio/tone.wav `
  --audio-smoke-ms 1000
```

This validates the desktop shell boundary only. `tone.wav` is not a product
codec acceptance asset. Product audio codecs, I2S and playback tasks remain
host/port responsibilities.
