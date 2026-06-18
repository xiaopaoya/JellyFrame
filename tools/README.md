# Tools

Desktop developer tools for packaging, validation and editor integration.

- `jellyframe_cli.py`: command-line helper for app/package workflows.
- `package_app.py`: package builder and validator.
- `native/`: C++ inspection tools, pseudo browser, Win32 shell and font-pack
  generator.
- `vscode-jellyframe/`: VS Code extension helper.

Tools may use Python, Node.js or desktop file I/O. The embedded runtime must not
depend on them.
