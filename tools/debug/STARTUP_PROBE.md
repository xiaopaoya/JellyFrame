# Author Startup Probe

> Last updated: 2026-09-20; Applies to: 0.6.0-dev

For unexpectedly slow preview/debug startup on Windows, keep the existing SDK,
App, security settings and VM configuration unchanged. This probe uses the SDK's
embedded Python; no system Python, pip, VS Code update or firmware change is needed.

1. Extract the diagnostic ZIP into a local directory in the affected VM.
2. Double-click `author_startup_probe.cmd` and enter the SDK and App directories.
3. Let three repeats complete. Return the generated `Documents/JellyFrame-startup-*`
   directory, especially `summary.json` and the timestamped logs.

The probe measures interpreter startup, native executable startup, validation,
preview subprocesses and debug first-frame announcement. It opens a hidden debug
session and sends `quit` after its first frame. A 90-second timeout terminates
the probe's own process tree. It never connects to or flashes a device. Use a
trusted template App; debugging executes that App's JavaScript.

Only the output directory is written; App and SDK files are not changed. Reports
contain local paths and App logs: review them before sharing. Capture files may
contain App content; screenshots are not required as evidence.

For an explicit target or output directory:

```powershell
& '<SDK>/runtime/python/python.exe' ./author_startup_probe.py --sdk '<SDK>' --app '<APP>' --target round-300 --output '<NEW OUTPUT DIRECTORY>'
```

Also report the template name, SDK/App locations (local disk, VM shared folder or
synced directory), and whether repeating the same command is faster. The first
observed run is not guaranteed to be a cold-cache run. These are host wall-clock
measurements, not pipeline CPU time or device FPS. Antivirus, filesystem delays,
dynamic-library loading and rendering are not inferred from total time alone.
