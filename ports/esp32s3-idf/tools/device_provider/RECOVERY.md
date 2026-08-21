# WS147 Factory Recovery

This procedure applies only to the exact Developer Image manifest included in
the same release archive. Verify `SHA256SUMS.txt` before using it.

1. Disconnect any program currently using the board's native USB
   Serial/JTAG endpoint.
2. Install the ESP-IDF-compatible `esptool` host utility.
3. Put the WS147 board in download mode if the normal reset handshake cannot
   connect it.
4. Write the complete `recovery/ws147-factory-16mb.bin` image at offset zero:

```powershell
python -m esptool --chip esp32s3 -p COMx --before default_reset --after hard_reset `
  write_flash --flash_size 16MB 0x0 recovery\ws147-factory-16mb.bin
```

5. Verify that the board starts the protected launcher. Then use the installed
   `jellyframe-device.cmd` provider to run `discover`, `info`, and a normal
   JFDP/1 install. Do not use this recovery operation to write arbitrary flash
   offsets.

The factory image intentionally erases installed third-party Apps and their
storage. It does not contain credentials or private keys.
