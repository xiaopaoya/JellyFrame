# README Gallery Evidence

The four PNG files beside this note are produced by the current
`jellyframe_desktop_shell`, not by a browser or design mockup. Their precise
provenance is recorded in `gallery-provenance.json` and is regenerated with:

```powershell
python tools\generate_readme_gallery.py `
  --desktop-shell build\script-engine-desktop\Debug\jellyframe_desktop_shell.exe
```

The command captures `tools/templates/apps/weather`, `clock`, `timer` and
`calculator` at `300x300`, converts deterministic PPM captures to PNG and
updates the provenance record. Run it only from a freshly built shell at the
revision being documented.
