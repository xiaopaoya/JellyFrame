# WS147 USB Factory Recovery V1

This procedure restores the `org.jellyframe.ws147.developer` `0.1.0-dev`
image for the Waveshare ESP32-S3-Touch-LCD-1.47 (WS147). It applies only to a
16 MB flash board with the 172x320 JD9853 panel. Do not use it for WS169 or a
different ESP32-S3 flash layout.

## Materials

- `ws147-developer-image-factory-0.1.0-dev.bin`, a raw 16 MB image written at
  offset `0x0`.
- SHA-256:
  `6f3360753422c60ba32a1cee92fd01575e64ae41433acc31b9fb35d527bad2e1`
- `esptool.py` 4.12.0 from ESP-IDF v5.3.1.
- A data-capable USB cable and the board's native USB Serial/JTAG port.

The factory image contains the bootloader, partition table and factory app.
It overwrites the complete 16 MB address range. This permanently removes NVS,
installed bundles, rollback data, storage, user settings and any local assets.

## Confirm The Artifact

In PowerShell, before connecting the board for recovery:

```powershell
(Get-FileHash .\ws147-developer-image-factory-0.1.0-dev.bin -Algorithm SHA256).Hash.ToLower()
```

Proceed only when the result exactly matches the SHA-256 above. A mismatch is a
release-integrity failure; do not flash it.

## Recover

1. Disconnect any power source other than the recovery USB cable. Disconnect
   other serial monitors or JFDP clients.
2. Connect the WS147 and identify its COM port. If the chip does not connect,
   hold `BOOT`, tap `RST`, then release `BOOT` after the host begins
   connecting.
3. Run the following command, replacing `COMx` and the image path:

```powershell
python -m esptool --chip esp32s3 -p COMx -b 460800 `
  --before default_reset --after hard_reset write_flash `
  --flash_mode dio --flash_freq 80m --flash_size 16MB `
  0x0 .\ws147-developer-image-factory-0.1.0-dev.bin
```

4. Confirm that esptool prints `Hash of data verified` and `Hard resetting`.
   If either is absent, repeat the procedure from step 2; do not claim a
   recovered device.
5. Reopen the JFDP/1 client. The recovered image exposes USB Serial/JTAG,
   reports board `ws147`, profile `rect-172x320`, transport `JFDP/1`, six
   feature families from the accompanying manifest and a 327680-byte bundle
   ceiling. The app library is initially empty.
6. Install a signed/validated Developer Image fixture through JFDP/1 and read
   AppList before launch. A failed install must leave the protected launcher
   usable; do not use raw flash to install an app.

## Observed Candidate

The recovery material was produced from source revision
`fbf10784ac8ce38f41ced40fa013a43564c992c8`, ESP-IDF v5.3.1, CPU 240 MHz,
8 MB octal PSRAM at 40 MHz, 16 MB flash, and the WS147 lifecycle profile with
fault point `0`. Its factory app SHA-256 is
`e7eb9b16cce5d9e781fc93717826192781b652704e91d1859f353f74e5cfaacc`.

This procedure recovers the Developer Image and protected launcher. It does
not validate panel rendering, touch calibration, battery behavior, or a future
Device OS provider implementation.
