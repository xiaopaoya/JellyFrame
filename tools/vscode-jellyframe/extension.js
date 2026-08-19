const fs = require("fs");
const path = require("path");
const childProcess = require("child_process");
const vscode = require("vscode");
const { selectBuildDirectory } = require("./build_profiles");

let outputChannel;
let reportPanel;
let capabilityDiagnostics;
let lastReport;
let lastReportCommand;
let lastPackageRoot;
let lastCapturePath;
let lastDeviceDiscovery;
let statusProvider;
let embeddedDebugSession;

function config() {
  return vscode.workspace.getConfiguration("jellyframe");
}

function repoRoot(context) {
  const configured = config().get("repoRoot", "").trim();
  return configured ? configured : path.resolve(context.extensionPath, "..", "..");
}

function cliPath(context) {
  return path.join(repoRoot(context), "tools", "jellyframe_cli.py");
}

function buildDir(context) {
  return path.join(repoRoot(context), "build");
}

function ensureBuildDir(context) {
  fs.mkdirSync(buildDir(context), { recursive: true });
}

function appRequiresScripting(root) {
  try {
    const manifestPath = packageManifestPath(root);
    if (!manifestPath) {
      return false;
    }
    const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
    const mode = manifest?.runtime?.script ?? manifest?.script;
    return typeof mode === "string" && mode !== "" && mode !== "none";
  } catch (_) {
    return false;
  }
}

function nativeBuildDir(context, preferScripting = false) {
  const configured = config().get("buildDir", "").trim();
  const resolvedConfigured = configured
    ? (path.isAbsolute(configured) ? configured : path.resolve(repoRoot(context), configured))
    : "";
  return selectBuildDirectory(repoRoot(context), resolvedConfigured, preferScripting);
}

function buildDirectoryError(context, selection) {
  const chinese = /^zh(?:-|$)/i.test(vscode.env.language || "");
  const code = selection?.issue?.code;
  if (code === "legacy-script-task-option") {
    return chinese
      ? "桌面构建仍使用已废弃的 JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME。请清除该构建目录后，使用 JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME 重新配置。"
      : "The desktop build still uses deprecated JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME. Clear that build directory and reconfigure with JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME.";
  }
  if (code === "scripting-disabled") {
    return chinese
      ? "当前 App 需要脚本构建，但所选桌面构建未启用 JELLYFRAME_BUILD_SCRIPTING=ON。"
      : "This App needs a scripting build, but the selected desktop build was configured without JELLYFRAME_BUILD_SCRIPTING=ON.";
  }
  if (code === "missing-directory" || code === "missing-cache") {
    return chinese
      ? "所选桌面构建不是可用的 CMake 构建输出。请在设置中选择实际的 Release/Debug 输出目录。"
      : "The selected desktop build is not a usable CMake build output. Choose the actual Release/Debug output directory in Settings.";
  }
  if (selection?.issue?.requiresScripting) {
    return chinese
      ? "未找到当前的脚本桌面构建。请配置 build/desktop-scripting-release（或 desktop-scripting-debug），并启用 JELLYFRAME_BUILD_SCRIPTING=ON。"
      : "No current scripting desktop build was found. Configure build/desktop-scripting-release (or desktop-scripting-debug) with JELLYFRAME_BUILD_SCRIPTING=ON.";
  }
  return chinese
    ? "未找到当前的桌面构建。请配置 build/desktop-release 或 build/desktop-debug。"
    : "No current desktop build was found. Configure build/desktop-release or build/desktop-debug.";
}

function requireNativeBuildDir(context, preferScripting = false) {
  const selection = nativeBuildDir(context, preferScripting);
  if (selection.buildDirectory) {
    return selection.buildDirectory;
  }
  const message = buildDirectoryError(context, selection);
  ensureOutputChannel().appendLine(`JellyFrame build selection: ${message}`);
  vscode.window.showErrorMessage(message);
  return undefined;
}

function debugLauncherPath(context) {
  return path.join(repoRoot(context), "tools", "debug", "jellyframe_debug.py");
}

function ensureOutputChannel(reveal = false) {
  if (!outputChannel) {
    outputChannel = vscode.window.createOutputChannel("JellyFrame");
  }
  if (reveal) {
    outputChannel.show(true);
  }
  return outputChannel;
}

function showOutputChannel() {
  ensureOutputChannel(true);
}

function discoverDevice(context) {
  const provider = config().get("deviceProvider", "").trim();
  if (!provider || !path.isAbsolute(provider)) {
    vscode.window.showWarningMessage("Configure JellyFrame: Device Provider with an absolute provider path first.");
    return;
  }
  const args = ["device", "--provider", provider];
  const manifest = config().get("deviceManifest", "").trim();
  if (manifest) {
    args.push("--manifest", path.isAbsolute(manifest) ? manifest : path.resolve(repoRoot(context), manifest));
  }
  args.push("discover");
  runCliWithOptions(context, args, {
    onStdout: (stdout) => {
      try {
        const result = JSON.parse(stdout);
        lastDeviceDiscovery = Array.isArray(result.devices) ? result.devices : [];
        statusProvider?.refresh();
      } catch (_) {
        lastDeviceDiscovery = undefined;
      }
    }
  });
}

function runCli(context, args) {
  return runCliWithOptions(context, args, {});
}

function runCliWithOptions(context, args, options) {
  const python = config().get("pythonPath", "python");
  const cli = cliPath(context);
  const channel = ensureOutputChannel();
  const commandArgs = [cli, ...args];
  if (options.reportPath && fs.existsSync(options.reportPath)) {
    fs.rmSync(options.reportPath, { force: true });
  }
  channel.appendLine(`+ ${[python, ...commandArgs].join(" ")}`);
  const child = childProcess.spawn(python, commandArgs, {
    cwd: repoRoot(context),
    shell: false
  });
  let failedToStart = false;
  let stdout = "";
  child.stdout.on("data", (chunk) => {
    const text = chunk.toString();
    stdout += text;
    channel.append(text);
  });
  child.stderr.on("data", (chunk) => {
    const text = chunk.toString();
    channel.append(text);
  });
  child.on("error", (error) => {
    failedToStart = true;
    channel.appendLine(`JellyFrame command failed to start: ${error.message}`);
    vscode.window.showErrorMessage(`JellyFrame command failed to start: ${error.message}`);
  });
  child.on("close", (code) => {
    channel.appendLine(`JellyFrame command exited with code ${code}`);
    if (failedToStart) {
      return;
    }
    if (options.reportPath && fs.existsSync(options.reportPath)) {
      loadReport(options.reportPath, options.commandName);
    }
    if (options.packageRoot && options.reportPath && fs.existsSync(options.reportPath)) {
      updateReportDiagnostics(options.packageRoot);
      showReportPanel(context);
    }
    if (code === 0 && options.capture && config().get("openCaptureAfterRun", true)) {
      openCaptureFile(options.capture);
    }
    if (code !== 0) {
      vscode.window.showErrorMessage(`JellyFrame command failed with code ${code}`);
    }
    if (options.onClose) {
      options.onClose(code);
    }
    if (code === 0 && options.onStdout) {
      options.onStdout(stdout);
    }
  });
}

function loadReport(reportPath, commandName) {
  try {
    if (fs.existsSync(reportPath)) {
      lastReport = JSON.parse(fs.readFileSync(reportPath, "utf8"));
      lastReportCommand = commandName || undefined;
    }
  } catch (error) {
    ensureOutputChannel().appendLine(`failed to read report ${reportPath}: ${error.message}`);
  }
}

function runDetachedPython(context, script, args, options = {}) {
  if (!fs.existsSync(script)) {
    vscode.window.showErrorMessage(`Missing JellyFrame debug tool: ${script}`);
    return;
  }
  const python = config().get("pythonPath", "python");
  const channel = ensureOutputChannel();
  const commandArgs = [script, ...args];
  channel.appendLine(`+ ${[python, ...commandArgs].join(" ")}`);
  const child = childProcess.spawn(python, commandArgs, {
    cwd: repoRoot(context),
    detached: !options.wait,
    stdio: options.wait ? "pipe" : "ignore",
    windowsHide: false
  });
  if (options.wait) {
    child.stdout.on("data", (chunk) => channel.append(chunk.toString()));
    child.stderr.on("data", (chunk) => channel.append(chunk.toString()));
  }
  child.on("error", (error) => {
    channel.appendLine(`JellyFrame debug command failed to start: ${error.message}`);
    vscode.window.showErrorMessage(`JellyFrame debug command failed to start: ${error.message}`);
  });
  child.on("close", (code) => {
    channel.appendLine(`JellyFrame debug command exited with code ${code}`);
    if (code !== 0) {
      vscode.window.showErrorMessage(`JellyFrame debug command failed with code ${code}`);
    }
    if (options.capture && code === 0 && config().get("openCaptureAfterRun", true)) {
      openCaptureFile(options.capture);
    }
    if (options.onClose) {
      options.onClose(code);
    }
  });
  if (!options.wait) {
    child.unref();
  }
}

function openCaptureFile(filePath) {
  if (!filePath || !fs.existsSync(filePath)) {
    vscode.window.showWarningMessage(`Capture not found: ${filePath || "none"}`);
    return;
  }
  lastCapturePath = filePath;
  vscode.commands.executeCommand("vscode.open", vscode.Uri.file(filePath));
}

function workspaceFolderPath() {
  const folders = vscode.workspace.workspaceFolders;
  if (!folders || folders.length === 0) {
    return undefined;
  }
  return folders[0].uri.fsPath;
}

function findPackageRootFrom(startPath) {
  if (!startPath || !fs.existsSync(startPath)) {
    return undefined;
  }
  let current;
  try {
    current = fs.statSync(startPath).isDirectory() ? startPath : path.dirname(startPath);
  } catch (_) {
    return undefined;
  }
  while (true) {
    if (isPackageRoot(current)) {
      return current;
    }
    const parent = path.dirname(current);
    if (parent === current) {
      return undefined;
    }
    current = parent;
  }
}

function isPackageRoot(root) {
  return Boolean(packageManifestPath(root));
}

function packageManifestPath(root) {
  if (!root) {
    return undefined;
  }
  for (const name of ["jellyframe.app.json", "app.json"]) {
    const candidate = path.join(root, name);
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }
  return undefined;
}

function currentPackageRoot() {
  const active = vscode.window.activeTextEditor?.document.uri.fsPath;
  const activeRoot = findPackageRootFrom(active);
  if (activeRoot) {
    lastPackageRoot = activeRoot;
    return activeRoot;
  }
  const workspaceRoot = findPackageRootFrom(workspaceFolderPath());
  if (workspaceRoot) {
    lastPackageRoot = workspaceRoot;
    return workspaceRoot;
  }
  return lastPackageRoot && isPackageRoot(lastPackageRoot)
    ? lastPackageRoot
    : undefined;
}

async function packageRoot(resourceUri) {
  const selectedResource = resourceUri && resourceUri.fsPath;
  if (selectedResource) {
    const found = findPackageRootFrom(selectedResource);
    if (found) {
      lastPackageRoot = found;
      return found;
    }
  }
  const active = vscode.window.activeTextEditor?.document.uri.fsPath;
  if (active) {
    const found = findPackageRootFrom(active);
    if (found) {
      lastPackageRoot = found;
      return found;
    }
  }
  const workspace = workspaceFolderPath();
  if (workspace) {
    const found = findPackageRootFrom(workspace);
    if (found) {
      lastPackageRoot = found;
      return found;
    }
  }
  const selected = await vscode.window.showOpenDialog({
    canSelectFiles: false,
    canSelectFolders: true,
    canSelectMany: false,
    openLabel: "Select JellyFrame package root"
  });
  lastPackageRoot = selected && selected[0] ? selected[0].fsPath : undefined;
  return lastPackageRoot;
}

function outputBase(root) {
  return path.basename(root).replace(/[^a-zA-Z0-9_.-]/g, "_") || "app";
}

async function target() {
  return vscode.window.showInputBox({
    prompt: "JellyFrame target preset",
    value: config().get("defaultTarget", "round-300")
  });
}

async function selectFrameScript(root, purpose) {
  const chinese = /^zh(?:-|$)/i.test(vscode.env.language || "");
  const choice = await vscode.window.showQuickPick([
    {
      label: chinese ? "静态" : "Static",
      description: chinese ? "只检查当前入口页面" : "Inspect the current entry page only",
      value: "static"
    },
    {
      label: chinese ? "程控回放" : "Programmed playback",
      description: chinese ? "使用 .jfcapture 驱动交互并生成报告" : "Drive interaction with a .jfcapture script and generate a report",
      value: "scripted"
    }
  ], {
    placeHolder: purpose,
    ignoreFocusOut: true
  });
  if (!choice || choice.value === "static") {
    return undefined;
  }
  const selected = await vscode.window.showOpenDialog({
    defaultUri: vscode.Uri.file(root),
    canSelectFiles: true,
    canSelectFolders: false,
    canSelectMany: false,
    filters: { "JellyFrame frame scripts": ["jfcapture"] },
    openLabel: chinese ? "选择程控脚本" : "Select playback script"
  });
  return selected && selected[0] ? selected[0].fsPath : undefined;
}

async function runPackageCommand(context, commandName, resourceUri) {
  const root = await packageRoot(resourceUri);
  if (!root) {
    return;
  }
  const selectedTarget = commandName === "validate" ? undefined : await target();
  if (commandName !== "validate" && !selectedTarget) {
    return;
  }
  ensureBuildDir(context);
  const base = outputBase(root);
  const report = path.join(buildDir(context), `vscode-${base}-${commandName}-report.json`);
  const args = [commandName, "--root", root, "--report", report];
  if (selectedTarget) {
    args.push("--target", selectedTarget);
  }
  const options = {
    commandName,
    packageRoot: root,
    reportPath: report
  };
  if (commandName === "check") {
    args.push("--font-budget", config().get("fontBudget", "16x16"));
    const frameScript = await selectFrameScript(root, /^zh(?:-|$)/i.test(vscode.env.language || "")
      ? "选择渲染验证方式"
      : "Choose render verification mode");
    if (frameScript) {
      const nativeBuildDirectory = requireNativeBuildDir(context, appRequiresScripting(root));
      if (!nativeBuildDirectory) {
        return;
      }
      const frameOutputDir = path.join(buildDir(context), "debug", `${base}-check-frames`);
      const montage = path.join(buildDir(context), "debug", `${base}-check-montage.bmp`);
      args.push(
        "--build-dir", nativeBuildDirectory,
        "--frame-script", frameScript,
        "--frame-output-dir", frameOutputDir,
        "--frame-montage", montage
      );
      options.capture = montage;
    }
  }
  if (commandName === "package") {
    args.push(
      "--output-cpp",
      path.join(buildDir(context), `vscode-${base}-resources.cpp`),
      "--debug-dir",
      path.join(buildDir(context), `vscode-${base}.jfdir`)
    );
  }
  runCliWithOptions(context, args, options);
}

async function previewPackage(context, resourceUri) {
  const root = await packageRoot(resourceUri);
  if (!root) {
    return;
  }
  const selectedTarget = await target();
  if (!selectedTarget) {
    return;
  }
  const frameScript = await selectFrameScript(root, /^zh(?:-|$)/i.test(vscode.env.language || "")
    ? "选择预览方式"
    : "Choose preview mode");
  ensureBuildDir(context);
  const base = outputBase(root);
  const output = path.join(buildDir(context), `vscode-${base}.bmp`);
  const report = path.join(buildDir(context), `vscode-${base}-preview-report.json`);
  const args = ["preview", "--root", root, "--target", selectedTarget, "--output", output, "--report", report];
  if (frameScript) {
    args.push(
      "--frame-script", frameScript,
      "--frame-output-dir", path.join(buildDir(context), "debug", `${outputBase(root)}-preview-frames`)
    );
  }
  runCliWithOptions(context, args, {
    commandName: "preview",
    packageRoot: root,
    reportPath: report,
    capture: output
  });
}

async function debugExternalApp(context, resourceUri) {
  if (process.platform !== "win32") {
    vscode.window.showErrorMessage("JellyFrame desktop shell is only available on Windows.");
    return;
  }
  const root = await packageRoot(resourceUri);
  if (!root) {
    return;
  }
  const selectedTarget = await target();
  if (!selectedTarget) {
    return;
  }
  const launcher = debugLauncherPath(context);
  if (!fs.existsSync(launcher)) {
    vscode.window.showErrorMessage(`Missing debug launcher: ${launcher}`);
    return;
  }
  const nativeBuildDirectory = requireNativeBuildDir(context, appRequiresScripting(root));
  if (!nativeBuildDirectory) {
    return;
  }
  ensureBuildDir(context);
  const base = outputBase(root);
  const runtimeLog = path.join(buildDir(context), `vscode-${base}-debug-runtime.log`);
  const report = path.join(buildDir(context), `vscode-${base}-debug-report.json`);
  runDetachedPython(context, launcher, [
    "--build-dir", nativeBuildDirectory,
    "--app", root,
    "--runtime-log", runtimeLog,
    "--wait"
  ], {
    wait: true,
    onClose: (code) => {
      if (code !== 0) {
        return;
      }
      runCliWithOptions(context, [
        "check",
        "--root", root,
        "--target", selectedTarget,
        "--build-dir", nativeBuildDirectory,
        "--report", report,
        "--runtime-log", runtimeLog,
        "--font-budget", config().get("fontBudget", "16x16")
      ], {
        commandName: "debug",
        packageRoot: root,
        reportPath: report
      });
    }
  });
}

function embeddedDebugHtml(webview) {
  const nonce = `${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const chinese = /^zh(?:-|$)/i.test(vscode.env.language || '');
  const recordIdle = chinese ? '录制' : 'Record';
  const recordActive = chinese ? '停止录制' : 'Stop recording';
  return `<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';">
  <style>
    html, body { width: 100%; height: 100%; overflow: hidden; }
    body { --sidebar-width: 340px; --diagnostics-height: 290px; --diagnostics-green: #8fd694; box-sizing: border-box; margin: 0; color: var(--vscode-foreground); background: var(--vscode-editor-background); font-family: var(--vscode-font-family); display: grid; grid-template-rows: 42px minmax(0, 1fr) 6px var(--diagnostics-height); }
    header { display: flex; align-items: center; gap: 12px; padding: 0 12px; border-bottom: 1px solid var(--vscode-panel-border); font-size: 12px; }
    #status { color: var(--vscode-descriptionForeground); flex: 1; }
    button { appearance: none; min-width: 28px; min-height: 26px; border: 1px solid var(--vscode-button-border, transparent); color: var(--vscode-button-foreground); background: var(--vscode-button-background); cursor: pointer; }
    button:hover { background: var(--vscode-button-hoverBackground); }
    #view-controls { display: flex; align-items: center; gap: 5px; }
    #view-controls button { min-width: 30px; padding: 0 8px; }
    #zoom-fit { min-width: 44px !important; }
    #zoom-label { min-width: 54px; color: var(--vscode-descriptionForeground); font-variant-numeric: tabular-nums; text-align: right; }
    #workspace { min-width: 0; min-height: 0; display: grid; grid-template-columns: minmax(0, 1fr) 6px var(--sidebar-width); }
    #stage { min-width: 0; min-height: 0; display: flex; flex-direction: column; overflow: hidden; }
    #stage-bar { display: flex; align-items: center; gap: 8px; min-height: 34px; box-sizing: border-box; padding: 0 10px; border-bottom: 1px solid var(--vscode-panel-border); }
    .workspace-tab { min-height: 26px; padding: 0 9px; border-color: transparent; color: var(--vscode-descriptionForeground); background: transparent; font-size: 12px; }
    .workspace-tab.active { color: var(--vscode-foreground); border-bottom: 2px solid var(--vscode-focusBorder); }
    #stage-controls { display: flex; align-items: center; gap: 5px; margin-left: auto; }
    #stage-controls button { min-width: 30px; padding: 0 8px; }
    #record.recording { color: var(--vscode-button-foreground); background: var(--vscode-testing-iconFailed, #c74e39); }
    #stage-content { flex: 1; min-width: 0; min-height: 0; display: grid; place-items: center; padding: 18px; overflow: auto; }
    #frame { display: block; flex: none; user-select: none; outline: none; background: #111; image-rendering: auto; }
    #empty { color: var(--vscode-descriptionForeground); }
    .resizer { position: relative; z-index: 2; background: transparent; }
    .resizer:hover, .resizer:active { background: var(--vscode-focusBorder); }
    #side-resizer { cursor: col-resize; }
    #bottom-resizer { cursor: row-resize; }
    #log-panel { min-width: 0; min-height: 0; display: flex; flex-direction: column; border-left: 1px solid var(--vscode-panel-border); }
    #log-bar, #diagnostics-title { display: flex; align-items: center; gap: 6px; min-height: 34px; box-sizing: border-box; padding: 0 10px; color: var(--vscode-descriptionForeground); font-size: 12px; border-bottom: 1px solid var(--vscode-panel-border); }
    #log-title { flex: 1; }
    #log-bar button { min-width: 42px; min-height: 24px; padding: 0 7px; font-size: 11px; }
    #log-filters { display: flex; gap: 0; padding: 5px 8px; border-bottom: 1px solid var(--vscode-panel-border); overflow-x: auto; }
    .log-filter { min-width: auto; min-height: 25px; padding: 0 7px; border-color: transparent; color: var(--vscode-descriptionForeground); background: transparent; font-size: 11px; }
    .log-filter.active { color: var(--vscode-button-foreground); background: var(--vscode-button-background); }
    #log { flex: 1; min-height: 0; padding: 8px 10px; overflow: auto; font: 13px/1.45 var(--vscode-editor-font-family, monospace); background: transparent; }
    .log-entry { display: flex; align-items: flex-start; gap: 7px; padding: 3px 0; word-break: break-word; }
    .log-kind { flex: 0 0 auto; min-width: 42px; box-sizing: border-box; padding: 1px 4px; border-radius: 2px; color: var(--vscode-descriptionForeground); background: var(--vscode-textBlockQuote-background); font: 10px/1.35 var(--vscode-font-family); text-align: center; text-transform: uppercase; }
    .log-entry.info .log-kind { color: var(--vscode-terminal-ansiCyan, #75beff); }
    .log-entry.event .log-kind { color: var(--vscode-terminal-ansiBlue, #75beff); }
    .log-entry.warning .log-kind { color: var(--vscode-terminal-ansiYellow, #cca700); }
    .log-entry.error .log-kind { color: var(--vscode-terminal-ansiRed, #f48771); }
    .log-entry.system .log-kind { color: var(--vscode-terminal-ansiGreen, #89d185); }
    .log-text { min-width: 0; }
    #diagnostics { min-height: 0; border-top: 1px solid var(--vscode-panel-border); color: var(--diagnostics-green); }
    #diagnostics-title { min-height: 38px; color: var(--diagnostics-green); font-size: 13px; border-bottom-color: var(--vscode-panel-border); }
    #diagnostics-text { height: calc(100% - 38px); box-sizing: border-box; margin: 0; padding: 11px 14px; overflow: auto; white-space: pre-wrap; word-break: break-word; font: 13px/1.55 var(--vscode-editor-font-family, monospace); }
    @media (max-width: 700px) {
      body { --sidebar-width: 260px; --diagnostics-height: 250px; }
      #workspace { grid-template-columns: minmax(0, 1fr) 4px var(--sidebar-width); }
      #stage-content { padding: 10px; }
    }
  </style>
</head>
<body>
  <header><strong>JellyFrame</strong><span id="status">Starting desktop shell...</span></header>
  <section id="workspace"><main id="stage"><div id="stage-bar"><button class="workspace-tab active" aria-selected="true">App viewport</button><div id="stage-controls"><button id="record" title="Record semantic interactions">${recordIdle}</button><button id="zoom-out" title="Zoom out">-</button><button id="zoom-fit" title="Fit to available space">Fit</button><button id="zoom-in" title="Zoom in">+</button><span id="zoom-label">Fit</span></div></div><div id="stage-content"><span id="empty">Waiting for the first frame...</span><canvas id="frame" tabindex="0" hidden aria-label="JellyFrame app frame"></canvas></div></main><div id="side-resizer" class="resizer" role="separator" aria-label="Resize live log"></div><aside id="log-panel"><div id="log-bar"><span id="log-title">Live log</span><button id="clear-log" title="Clear live log">Clear</button><button id="stop" title="Stop desktop shell">Stop</button></div><div id="log-filters"><button class="log-filter active" data-filter="all">All</button><button class="log-filter" data-filter="info">Info</button><button class="log-filter" data-filter="event">Events</button><button class="log-filter" data-filter="warning">Warnings</button><button class="log-filter" data-filter="error">Errors</button></div><div id="log" role="log" aria-live="polite"></div></aside></section>
  <div id="bottom-resizer" class="resizer" role="separator" aria-label="Resize session diagnostics"></div>
  <section id="diagnostics"><div id="diagnostics-title">Session diagnostics</div><pre id="diagnostics-text">Waiting for session configuration...</pre></section>
  <script nonce="${nonce}">
    const vscode = acquireVsCodeApi();
    const recordIdle = '${recordIdle}';
    const recordActive = '${recordActive}';
    const frame = document.getElementById('frame');
    const empty = document.getElementById('empty');
    const stage = document.getElementById('stage-content');
    const status = document.getElementById('status');
    const stop = document.getElementById('stop');
    const record = document.getElementById('record');
    const clearLog = document.getElementById('clear-log');
    const log = document.getElementById('log');
    const diagnostics = document.getElementById('diagnostics-text');
    const zoomOut = document.getElementById('zoom-out');
    const zoomFit = document.getElementById('zoom-fit');
    const zoomIn = document.getElementById('zoom-in');
    const zoomLabel = document.getElementById('zoom-label');
    const sideResizer = document.getElementById('side-resizer');
    const bottomResizer = document.getElementById('bottom-resizer');
    const viewState = vscode.getState ? (vscode.getState() || {}) : {};
    let viewport = { width: 1, height: 1 };
    let latestSequence = 0;
    let renderedSequence = 0;
    let renderToken = 0;
    let moveQueued = false;
    let pendingMove = null;
    let logLines = [];
    let logFilter = 'all';
    let recording = false;
    let fitMode = viewState.fitMode !== false;
    let manualZoom = Math.max(0.25, Math.min(4, Number(viewState.manualZoom) || 1));
    function persistViewState() {
      vscode.setState?.({
        sidebarWidth: parseFloat(document.body.style.getPropertyValue('--sidebar-width')) || 340,
        diagnosticsHeight: parseFloat(document.body.style.getPropertyValue('--diagnostics-height')) || 290,
        fitMode,
        manualZoom
      });
    }
    function restoreDimension(variable, value, minimum, maximum) {
      const numeric = Number(value);
      if (Number.isFinite(numeric)) document.body.style.setProperty(variable, Math.max(minimum, Math.min(maximum, numeric)) + 'px');
    }
    restoreDimension('--sidebar-width', viewState.sidebarWidth, 240, Math.max(260, window.innerWidth * 0.7));
    restoreDimension('--diagnostics-height', viewState.diagnosticsHeight, 190, Math.max(220, window.innerHeight - 160));
    function fittedScale() {
      const availableWidth = Math.max(1, stage.clientWidth - 36);
      const availableHeight = Math.max(1, stage.clientHeight - 36);
      return Math.max(0.25, Math.min(4, Math.min(availableWidth / viewport.width, availableHeight / viewport.height)));
    }
    function currentScale() {
      return fitMode ? fittedScale() : manualZoom;
    }
    function updateFrameSize() {
      if (frame.hidden) return;
      const scale = currentScale();
      frame.style.width = Math.max(1, Math.round(viewport.width * scale)) + 'px';
      frame.style.height = Math.max(1, Math.round(viewport.height * scale)) + 'px';
      zoomLabel.textContent = (fitMode ? 'Fit ' : '') + Math.round(scale * 100) + '%';
    }
    function setManualZoom(nextZoom) {
      fitMode = false;
      manualZoom = Math.max(0.25, Math.min(4, nextZoom));
      updateFrameSize();
      persistViewState();
    }
    function renderLog() {
      const keepPinned = log.scrollTop + log.clientHeight >= log.scrollHeight - 12;
      log.replaceChildren();
      const visible = logLines.filter((entry) => logFilter === 'all' || entry.category === logFilter);
      if (visible.length === 0) {
        const placeholder = document.createElement('div');
        placeholder.className = 'log-entry';
        placeholder.textContent = logLines.length === 0 ? 'Waiting for shell output...' : 'No matching entries.';
        log.appendChild(placeholder);
      } else {
        for (const entry of visible) {
          const row = document.createElement('div');
          row.className = 'log-entry ' + entry.category;
          const kind = document.createElement('span');
          kind.className = 'log-kind';
          kind.textContent = entry.label;
          const text = document.createElement('span');
          text.className = 'log-text';
          text.textContent = entry.text;
          row.append(kind, text);
          log.appendChild(row);
        }
      }
      if (keepPinned) log.scrollTop = log.scrollHeight;
    }
    function appendLog(entry) {
      logLines.push(entry);
      if (logLines.length > 200) logLines = logLines.slice(-200);
      renderLog();
    }
    function point(event) {
      const rect = frame.getBoundingClientRect();
      return {
        x: Math.max(0, Math.min(viewport.width - 1, Math.round((event.clientX - rect.left) * viewport.width / Math.max(1, rect.width)))),
        y: Math.max(0, Math.min(viewport.height - 1, Math.round((event.clientY - rect.top) * viewport.height / Math.max(1, rect.height))))
      };
    }
    function sendPointer(action, event) {
      if (frame.hidden) return;
      const p = point(event);
      vscode.postMessage({ type: 'input', line: 'pointer ' + action + ' ' + p.x + ' ' + p.y + (event.buttons ? ' 1' : ' 0') });
    }
    frame.addEventListener('pointerdown', (event) => { frame.focus(); frame.setPointerCapture?.(event.pointerId); sendPointer('down', event); event.preventDefault(); });
    frame.addEventListener('pointerup', (event) => { frame.releasePointerCapture?.(event.pointerId); sendPointer('up', event); event.preventDefault(); });
    frame.addEventListener('pointermove', (event) => {
      pendingMove = event;
      if (!moveQueued) {
        moveQueued = true;
        requestAnimationFrame(() => { moveQueued = false; if (pendingMove) sendPointer('move', pendingMove); pendingMove = null; });
      }
    });
    frame.addEventListener('wheel', (event) => {
      if (event.ctrlKey) {
        const step = event.deltaY < 0 ? 1.1 : 1 / 1.1;
        setManualZoom(currentScale() * step);
        event.preventDefault();
        return;
      }
      const p = point(event);
      const delta = Math.max(-120, Math.min(120, Math.round(-event.deltaY)));
      vscode.postMessage({ type: 'input', line: 'wheel ' + p.x + ' ' + p.y + ' ' + delta });
      event.preventDefault();
    }, { passive: false });
    frame.addEventListener('keydown', (event) => {
      const keys = { Escape: 'escape', Enter: 'enter', ' ': 'space', Tab: 'tab', ArrowUp: 'up', ArrowDown: 'down', Backspace: 'backspace' };
      if (keys[event.key]) { vscode.postMessage({ type: 'input', line: 'key ' + keys[event.key] }); event.preventDefault(); }
    });
    clearLog.addEventListener('click', () => {
      logLines = [];
      renderLog();
      vscode.postMessage({ type: 'clear-log' });
    });
    record.addEventListener('click', () => {
      recording = !recording;
      record.textContent = recording ? recordActive : recordIdle;
      record.classList.toggle('recording', recording);
      if (recording) {
        logFilter = 'event';
        document.querySelectorAll('.log-filter').forEach((item) => item.classList.toggle('active', item.dataset.filter === 'event'));
        renderLog();
      }
      vscode.postMessage({ type: recording ? 'record-start' : 'record-stop' });
    });
    stop.addEventListener('click', () => vscode.postMessage({ type: 'stop' }));
    zoomOut.addEventListener('click', () => setManualZoom(currentScale() / 1.2));
    zoomIn.addEventListener('click', () => setManualZoom(currentScale() * 1.2));
    zoomFit.addEventListener('click', () => { fitMode = true; updateFrameSize(); persistViewState(); });
    document.querySelectorAll('.log-filter').forEach((button) => button.addEventListener('click', () => {
      logFilter = button.dataset.filter || 'all';
      document.querySelectorAll('.log-filter').forEach((item) => item.classList.toggle('active', item === button));
      renderLog();
    }));
    function installResizer(element, variable, minimum, maximum) {
      element.addEventListener('pointerdown', (startEvent) => {
        const startValue = parseFloat(getComputedStyle(document.body).getPropertyValue(variable));
        const startPoint = variable === '--sidebar-width' ? startEvent.clientX : startEvent.clientY;
        const pointerId = startEvent.pointerId;
        element.setPointerCapture?.(pointerId);
        const move = (event) => {
          const point = variable === '--sidebar-width' ? event.clientX : event.clientY;
          const value = startValue - (point - startPoint);
          document.body.style.setProperty(variable, Math.max(minimum, Math.min(maximum(), value)) + 'px');
          updateFrameSize();
        };
        const finish = () => {
          element.removeEventListener('pointermove', move);
          element.removeEventListener('pointerup', finish);
          element.removeEventListener('pointercancel', finish);
          persistViewState();
        };
        element.addEventListener('pointermove', move);
        element.addEventListener('pointerup', finish);
        element.addEventListener('pointercancel', finish);
        startEvent.preventDefault();
      });
    }
    installResizer(sideResizer, '--sidebar-width', 240, () => Math.max(260, window.innerWidth * 0.7));
    installResizer(bottomResizer, '--diagnostics-height', 190, () => Math.max(220, window.innerHeight - 160));
    new ResizeObserver(() => updateFrameSize()).observe(stage);
    renderLog();
    vscode.postMessage({ type: 'ready' });
    window.addEventListener('message', (event) => {
      const message = event.data;
      if (message.type === 'frame' && message.sequence > latestSequence) {
        latestSequence = message.sequence;
        viewport = { width: message.width, height: message.height };
        const token = ++renderToken;
        const image = new Image();
        image.onload = () => {
          if (token !== renderToken || message.sequence <= renderedSequence) return;
          frame.width = message.width;
          frame.height = message.height;
          const context = frame.getContext('2d', { alpha: false });
          context.clearRect(0, 0, message.width, message.height);
          context.drawImage(image, 0, 0, message.width, message.height);
          renderedSequence = message.sequence;
          frame.hidden = false;
          empty.hidden = true;
          updateFrameSize();
          status.textContent = 'Frame ' + message.sequence + ' · ' + message.width + 'x' + message.height;
        };
        image.onerror = () => {
          if (token === renderToken) status.textContent = 'Frame ' + message.sequence + ' failed to decode';
        };
        image.src = message.dataUri;
      } else if (message.type === 'status') {
        status.textContent = message.text;
      } else if (message.type === 'log') {
        appendLog({
          category: message.category || 'info',
          label: message.label || 'Info',
          text: message.text || ''
        });
      } else if (message.type === 'clear-log') {
        logLines = [];
        renderLog();
      } else if (message.type === 'diagnostics') {
        diagnostics.textContent = message.text || '';
      } else if (message.type === 'record-state') {
        recording = Boolean(message.recording);
        record.textContent = recording ? recordActive : recordIdle;
        record.classList.toggle('recording', recording);
      }
    });
  </script>
</body>
</html>`;
}

function stopEmbeddedDebugSession(session, reason) {
  if (!session || session.stopping) {
    return session?.exitPromise || Promise.resolve();
  }
  session.exitPromise = new Promise((resolve) => { session.resolveExit = resolve; });
  session.stopping = true;
  session.stopReason = reason || 'user request';
  appendEmbeddedLog(session, 'lifecycle', `stop requested: ${session.stopReason}`);
  scheduleEmbeddedDiagnostics(session);
  try {
    postEmbeddedMessage(session, { type: 'status', text: session.stopReason });
  } catch (_) {
    // The panel may already be disposing; the process still needs to be stopped.
  }
  if (session.child.stdin?.writable) {
    session.child.stdin.write('quit\n');
    session.child.stdin.end();
  }
  session.forceStopTimer = setTimeout(() => {
    if (!session.exited) {
      session.stopReason = `${session.stopReason || 'stop'} · force-terminated after timeout`;
      appendEmbeddedLog(session, 'lifecycle', `shell did not exit in time; terminating process tree pid=${session.child.pid}`);
      scheduleEmbeddedDiagnostics(session);
      childProcess.spawn('taskkill', ['/pid', String(session.child.pid), '/t', '/f'], { windowsHide: true, stdio: 'ignore' });
    }
  }, 2500);
  return session.exitPromise;
}

function parseEmbeddedFrameLine(line) {
  const fields = line.split('\t');
  if (fields.length !== 5 || fields[0] !== 'JF_FRAME') {
    return undefined;
  }
  const sequence = Number(fields[1]);
  const width = Number(fields[2]);
  const height = Number(fields[3]);
  if (!Number.isSafeInteger(sequence) || !Number.isInteger(width) || !Number.isInteger(height) || sequence < 1 || width < 1 || height < 1) {
    return undefined;
  }
  return { sequence, width, height, path: fields[4] };
}

function postEmbeddedMessage(session, message) {
  try {
    session.panel.webview.postMessage(message);
  } catch (_) {
    // The panel can be disposing while the child is still draining output.
  }
}

function embeddedDiagnosticsText(session) {
  const elapsed = Math.max(0, Date.now() - session.startedAt);
  return [
    `App: ${session.appRoot}`,
    `Runtime: ${session.scriptMode === 'classic' ? 'Classic scripting' : 'No script runtime'} · Build: ${session.buildProfile}`,
    `Desktop shell: ${session.shellPath} (PID ${session.child.pid})`,
    `Launcher: ${session.launcher}`,
    `Frame cache: ${session.frameDir}`,
    `Session: ${session.exited ? 'Stopped' : session.stopping ? 'Stopping' : 'Running'} · ${elapsed} ms · exit ${session.exitCode ?? 'pending'}`,
    `Frames: ${session.deliveredFrames} displayed / ${session.announcedFrames} announced · ${session.droppedFrames} superseded · ${session.decodeErrors} read failures`,
    `Latest frame: ${session.lastDeliveredSequence} · ${session.viewport.width}x${session.viewport.height}`,
    `Input: ${session.inputSent} sent · Shell output: ${session.stdoutLines} standard, ${session.stderrLines} error lines`,
    `Semantic capture: ${session.recording ? 'Recording' : 'Idle'} · ${session.recordingActions.length} actions`,
    `Stop reason: ${session.stopReason || 'None'}`
  ].join('\n');
}

function scheduleEmbeddedDiagnostics(session) {
  if (session.diagnosticsScheduled) {
    return;
  }
  session.diagnosticsScheduled = true;
  setTimeout(() => {
    session.diagnosticsScheduled = false;
    postEmbeddedMessage(session, { type: 'diagnostics', text: embeddedDiagnosticsText(session) });
  }, 80);
}

function simplifyEmbeddedLogLine(stream, line) {
  const text = String(line).trim();
  if (!text || text.startsWith('+ ')) {
    return undefined;
  }
  const mediaMatch = text.match(/css::css-media-query-not-matched.*?\((@media .+)\)$/);
  if (mediaMatch) {
    return { category: 'info', label: 'CSS', text: `Media query not matched: ${mediaMatch[1]}` };
  }
  const diagnosticsMatch = text.match(/^diagnostics:\s*(\d+)/i);
  if (diagnosticsMatch) {
    return { category: 'info', label: 'Runtime', text: `Runtime diagnostics: ${diagnosticsMatch[1]}` };
  }
  const clickMatch = text.match(/^click target=(.+)$/i);
  if (clickMatch) {
    return { category: 'event', label: 'Event', text: `Click: ${clickMatch[1]}` };
  }
  const controlStateMatch = text.match(/^control state id=([^ ]+) kind=([^ ]+) value=(.*?) checked=(0|1) selected=(-?\d+)$/i);
  if (controlStateMatch) {
    const value = decodeCaptureValue(controlStateMatch[3]) || '(empty)';
    return {
      category: 'event',
      label: 'Control',
      text: controlStateMatch[2] + '#' + controlStateMatch[1] + ' = ' + value
    };
  }
  if (stream === 'lifecycle') {
    return { category: 'system', label: 'System', text };
  }
  if (stream === 'error' || /\[(?:error|fatal)\]/i.test(text)) {
    return { category: 'error', label: 'Error', text: text.replace(/^\[(?:error|fatal)\]\s*/i, '') };
  }
  if (/\[warning\]/i.test(text)) {
    return { category: 'warning', label: 'Warning', text: text.replace(/^\[warning\]\s*/i, '') };
  }
  return { category: 'info', label: 'Info', text: text.replace(/^\[info\]\s*/i, '') };
}

function captureFrameFor(session) {
  const sequence = session.latestAnnouncedSequence || session.announcedFrames || 1;
  const start = session.recordingStartSequence || sequence;
  return Math.max(1, sequence - start + 1);
}

function addRecordingAction(session, action) {
  const last = session.recordingActions[session.recordingActions.length - 1];
  const same = last && last.frame === action.frame && last.kind === action.kind &&
    last.id === action.id && last.value === action.value;
  if (!same) {
    session.recordingActions.push(action);
  }
}

function replacePendingControlAction(session, pending, action) {
  for (let index = session.recordingActions.length - 1; index >= 0; index -= 1) {
    const current = session.recordingActions[index];
    if (current.kind === 'click-id' && current.id === pending.id && current.frame === pending.frame) {
      break;
    }
    if (current.kind === action.kind && current.id === action.id) {
      session.recordingActions[index] = action;
      return;
    }
  }
  addRecordingAction(session, action);
}

function recordSemanticLog(session, text) {
  if (!session.recording) {
    return;
  }
  const click = String(text).match(/^click target=[^#\s]+#([A-Za-z0-9_-]+)(?:\.|$)/i);
  if (click) {
    const frame = captureFrameFor(session);
    session.recordingPendingClick = { id: click[1], frame };
    addRecordingAction(session, { frame, kind: 'click-id', id: click[1], value: undefined });
    return;
  }
  if (/^click target=/i.test(String(text))) {
    session.recordingSkipped += 1;
    return;
  }
  const state = String(text).match(/^control state id=([A-Za-z0-9_-]+) kind=([^ ]+) value=(.*?) checked=(0|1) selected=(-?\d+)$/i);
  if (!state || !session.recordingPendingClick || state[1] !== session.recordingPendingClick.id) {
    return;
  }
  const pending = session.recordingPendingClick;
  const kind = state[2].toLowerCase();
  if (kind === 'checkbox' || kind === 'radio') {
    replacePendingControlAction(session, pending, { frame: pending.frame, kind: 'set-checked', id: pending.id, value: state[4] });
  } else if (kind === 'select' && Number(state[5]) >= 0) {
    replacePendingControlAction(session, pending, { frame: pending.frame, kind: 'select-index', id: pending.id, value: state[5] });
  } else if (kind !== 'button') {
    replacePendingControlAction(session, pending, { frame: pending.frame, kind: 'set-value', id: pending.id, value: decodeCaptureValue(state[3]) });
  }
}

function encodeCaptureValue(value) {
  return encodeURIComponent(String(value ?? ''));
}

function decodeCaptureValue(value) {
  try {
    return decodeURIComponent(String(value ?? ''));
  } catch (_) {
    return String(value ?? '');
  }
}

async function saveEmbeddedRecording(context, session) {
  if (!session.recordingActions.length) {
    vscode.window.showInformationMessage('No semantic control actions were recorded.');
    return;
  }
  const chinese = /^zh(?:-|$)/i.test(vscode.env.language || '');
  const base = outputBase(session.appRoot);
  const selected = await vscode.window.showSaveDialog({
    defaultUri: vscode.Uri.file(path.join(session.appRoot, base + '.jfcapture')),
    filters: { 'JellyFrame captures': ['jfcapture'] },
    saveLabel: chinese ? '保存录制' : 'Save capture'
  });
  if (!selected) {
    return;
  }
  const maxFrame = Math.max(...session.recordingActions.map((action) => action.frame));
  const frames = Math.max(12, maxFrame + 8);
  const viewport = session.viewport.width > 1 && session.viewport.height > 1
    ? session.viewport
    : { width: 300, height: 300 };
  const outputBaseName = path.basename(selected.fsPath, path.extname(selected.fsPath)).replace(/[^a-zA-Z0-9_.-]/g, '_') || base;
  const lines = [
    '# JellyFrame semantic interaction capture',
    'output-dir out/' + outputBaseName + '_frames',
    'montage out/' + outputBaseName + '_montage.bmp',
    'frames ' + frames,
    'step-ms 33',
    'start-ms 1000',
    'viewport ' + viewport.width + ' ' + viewport.height,
    'columns 4',
    'gap 6',
    '',
    ...session.recordingActions.map((action) => {
      const suffix = action.kind === 'set-value' ? ' ' + encodeCaptureValue(action.value) :
        action.value === undefined ? '' : ' ' + action.value;
      return 'event ' + action.frame + ' ' + action.kind + ' ' + action.id + suffix;
    })
  ];
  try {
    await fs.promises.writeFile(selected.fsPath, lines.join('\n') + '\n', 'utf8');
    vscode.window.showInformationMessage((chinese ? '已保存语义录制: ' : 'Semantic capture saved: ') + selected.fsPath);
    if (session.recordingSkipped > 0) {
      vscode.window.showWarningMessage(chinese
        ? '有 ' + session.recordingSkipped + ' 次点击缺少稳定控件 id，未写入 capture。'
        : String(session.recordingSkipped) + ' clicks had no stable control id and were not recorded.');
    }
  } catch (error) {
    vscode.window.showErrorMessage((chinese ? '保存录制失败: ' : 'Failed to save capture: ') + error.message);
  }
}

function appendEmbeddedLog(session, stream, text) {
  const lines = String(text).split(/\r?\n/).filter((line) => line.length > 0);
  if (lines.length === 0) {
    return;
  }
  session.stdoutLines += stream === 'stdout' ? lines.length : 0;
  session.stderrLines += stream === 'stderr' ? lines.length : 0;
  for (const line of lines) {
    recordSemanticLog(session, line);
    const entry = simplifyEmbeddedLogLine(stream, line);
    if (entry) {
      session.logLines.push(entry);
      if (session.webviewReady) {
        postEmbeddedMessage(session, { type: 'log', ...entry });
      }
    }
    ensureOutputChannel().appendLine(`[embedded][${stream}] ${line}`);
  }
  if (session.logLines.length > 200) {
    session.logLines = session.logLines.slice(-200);
  }
  scheduleEmbeddedDiagnostics(session);
}

async function deliverEmbeddedFrame(session, frame) {
  if (!session.active || frame.sequence < session.latestAnnouncedSequence) {
    session.droppedFrames += 1;
    scheduleEmbeddedDiagnostics(session);
    return;
  }
  try {
    const bytes = await fs.promises.readFile(frame.path);
    if (!session.active || frame.sequence !== session.latestAnnouncedSequence || frame.sequence <= session.lastDeliveredSequence) {
      return;
    }
    session.lastDeliveredSequence = frame.sequence;
    session.deliveredFrames += 1;
    session.viewport = { width: frame.width, height: frame.height };
    postEmbeddedMessage(session, {
      type: 'frame',
      sequence: frame.sequence,
      width: frame.width,
      height: frame.height,
      dataUri: `data:image/bmp;base64,${bytes.toString('base64')}`
    });
    scheduleEmbeddedDiagnostics(session);
  } catch (error) {
    if (error?.code === 'ENOENT') {
      // The native shell retains only a bounded latest-frame cache. A newer
      // frame can supersede this snapshot before VS Code starts the read.
      session.droppedFrames += 1;
    } else {
      session.decodeErrors += 1;
      appendEmbeddedLog(session, 'error', `failed to read frame ${frame.sequence}: ${error.message}`);
    }
    scheduleEmbeddedDiagnostics(session);
  } finally {
    fs.rm(frame.path, { force: true }, () => {});
  }
}

async function debugApp(context, resourceUri) {
  if (process.platform !== 'win32') {
    vscode.window.showErrorMessage('JellyFrame desktop shell is only available on Windows.');
    return;
  }
  const root = await packageRoot(resourceUri);
  if (!root) {
    return;
  }
  const launcher = debugLauncherPath(context);
  if (!fs.existsSync(launcher)) {
    vscode.window.showErrorMessage(`Missing debug launcher: ${launcher}`);
    return;
  }
  const nativeBuildDirectory = requireNativeBuildDir(context, appRequiresScripting(root));
  if (!nativeBuildDirectory) {
    return;
  }
  if (embeddedDebugSession) {
    const previous = embeddedDebugSession;
    await stopEmbeddedDebugSession(previous, 'Replacing the previous debug session...');
  }
  ensureBuildDir(context);
  const sessionRoot = path.join(buildDir(context), 'debug', 'vscode-sessions');
  fs.mkdirSync(sessionRoot, { recursive: true });
  const frameDir = fs.mkdtempSync(path.join(sessionRoot, `${outputBase(root)}-`));
  const panel = vscode.window.createWebviewPanel(
    'jellyframeEmbeddedDebug',
    `JellyFrame: ${path.basename(root)}`,
    vscode.ViewColumn.Beside,
    { enableScripts: true, retainContextWhenHidden: true }
  );
  panel.webview.html = embeddedDebugHtml(panel.webview);
  const python = config().get('pythonPath', 'python');
  const scriptMode = appRequiresScripting(root) ? 'classic' : 'none';
  const shellPath = path.join(nativeBuildDirectory, process.platform === 'win32' ? 'jellyframe_desktop_shell.exe' : 'jellyframe_desktop_shell');
  const args = [launcher, '--build-dir', nativeBuildDirectory, '--app', root, '--vscode-debug', '--vscode-frame-dir', frameDir, '--wait'];
  const channel = ensureOutputChannel();
  channel.appendLine(`+ ${[python, ...args].join(' ')}`);
  const child = childProcess.spawn(python, args, {
    cwd: repoRoot(context),
    shell: false,
    windowsHide: true,
    stdio: ['pipe', 'pipe', 'pipe']
  });
  const session = {
    active: true,
    stopping: false,
    exited: false,
    child,
    panel,
    appRoot: root,
    scriptMode,
    buildProfile: path.basename(path.dirname(nativeBuildDirectory)),
    python,
    launcher,
    buildDir: nativeBuildDirectory,
    shellPath,
    frameDir,
    startedAt: Date.now(),
    viewport: { width: 1, height: 1 },
    announcedFrames: 0,
    deliveredFrames: 0,
    droppedFrames: 0,
    decodeErrors: 0,
    inputSent: 0,
    stdoutLines: 0,
    stderrLines: 0,
    logLines: [],
    webviewReady: false,
    diagnosticsScheduled: false,
    stopReason: undefined,
    exitCode: undefined,
    latestAnnouncedSequence: 0,
    lastDeliveredSequence: 0,
    recording: false,
    recordingStartSequence: 0,
    recordingActions: [],
    recordingPendingClick: undefined,
    recordingSkipped: 0,
    outputBuffer: '',
    forceStopTimer: undefined
  };
  embeddedDebugSession = session;
  appendEmbeddedLog(session, 'lifecycle', `spawn pid=${child.pid} profile=${session.buildProfile} script=${session.scriptMode}`);
  postEmbeddedMessage(session, { type: 'diagnostics', text: embeddedDiagnosticsText(session) });
  child.stdout.on('data', (chunk) => {
    session.outputBuffer += chunk.toString();
    let newline = 0;
    while ((newline = session.outputBuffer.indexOf('\n')) >= 0) {
      const line = session.outputBuffer.slice(0, newline).replace(/\r$/, '');
      session.outputBuffer = session.outputBuffer.slice(newline + 1);
      const frame = parseEmbeddedFrameLine(line);
      if (frame) {
        session.announcedFrames += 1;
        if (session.latestAnnouncedSequence > 0 && frame.sequence > session.latestAnnouncedSequence + 1) {
          session.droppedFrames += frame.sequence - session.latestAnnouncedSequence - 1;
        }
        session.latestAnnouncedSequence = Math.max(session.latestAnnouncedSequence, frame.sequence);
        void deliverEmbeddedFrame(session, frame);
      } else if (line) {
        appendEmbeddedLog(session, 'stdout', line);
      }
    }
  });
  child.stderr.on('data', (chunk) => appendEmbeddedLog(session, 'stderr', chunk.toString()));
  child.on('error', (error) => {
    appendEmbeddedLog(session, 'error', `failed to start: ${error.message}`);
    postEmbeddedMessage(session, { type: 'status', text: `Failed to start: ${error.message}` });
  });
  child.on('close', (code) => {
    session.exited = true;
    session.active = false;
    session.exitCode = code;
    if (session.forceStopTimer) {
      clearTimeout(session.forceStopTimer);
    }
    session.resolveExit?.();
    appendEmbeddedLog(session, 'lifecycle', `shell exited with code ${code ?? 'unknown'}`);
    postEmbeddedMessage(session, { type: 'status', text: `Desktop shell stopped (exit ${code ?? 'unknown'}).` });
    scheduleEmbeddedDiagnostics(session);
    if (embeddedDebugSession === session) {
      embeddedDebugSession = undefined;
    }
    setTimeout(() => fs.rm(frameDir, { recursive: true, force: true }, () => {}), 250);
  });
  panel.webview.onDidReceiveMessage((message) => {
    if (message?.type === 'ready') {
      session.webviewReady = true;
      postEmbeddedMessage(session, { type: 'diagnostics', text: embeddedDiagnosticsText(session) });
      for (const entry of session.logLines) {
        postEmbeddedMessage(session, { type: 'log', ...entry });
      }
    } else if (message?.type === 'stop') {
      stopEmbeddedDebugSession(session, 'Stopping desktop shell...');
    } else if (message?.type === 'clear-log') {
      session.logLines = [];
      postEmbeddedMessage(session, { type: 'clear-log' });
    } else if (message?.type === 'record-start') {
      session.recording = true;
      session.recordingStartSequence = session.latestAnnouncedSequence || session.announcedFrames || 0;
      session.recordingActions = [];
      session.recordingPendingClick = undefined;
      session.recordingSkipped = 0;
      postEmbeddedMessage(session, { type: 'record-state', recording: true });
      appendEmbeddedLog(session, 'lifecycle', 'semantic recording started');
    } else if (message?.type === 'record-stop') {
      session.recording = false;
      session.recordingPendingClick = undefined;
      postEmbeddedMessage(session, { type: 'record-state', recording: false });
      appendEmbeddedLog(session, 'lifecycle', 'semantic recording stopped');
      void saveEmbeddedRecording(context, session);
    } else if (message?.type === 'input' && typeof message.line === 'string' && /^[a-z]+(?: [a-z-]+)?(?: -?\d+){0,4}$/.test(message.line)) {
      if (session.active && !session.stopping && child.stdin?.writable) {
        child.stdin.write(`${message.line}\n`);
        session.inputSent += 1;
        scheduleEmbeddedDiagnostics(session);
      }
    }
  }, undefined, context.subscriptions);
  panel.onDidDispose(() => stopEmbeddedDebugSession(session, 'Debug tab closed.'), undefined, context.subscriptions);
}

async function runFrameScript(context, resourceUri) {
  if (process.platform !== "win32") {
    vscode.window.showErrorMessage("JellyFrame frame-script playback currently requires the desktop shell on Windows.");
    return;
  }
  const root = await packageRoot(resourceUri);
  if (!root) {
    return;
  }
  const selected = await vscode.window.showOpenDialog({
    defaultUri: vscode.Uri.file(root),
    canSelectFiles: true,
    canSelectFolders: false,
    canSelectMany: false,
    filters: { "JellyFrame frame scripts": ["jfcapture"] },
    openLabel: "Run Frame Script"
  });
  if (!selected || !selected[0]) {
    return;
  }
  const selectedTarget = await target();
  if (!selectedTarget) {
    return;
  }
  ensureBuildDir(context);
  const nativeBuildDirectory = requireNativeBuildDir(context, appRequiresScripting(root));
  if (!nativeBuildDirectory) {
    return;
  }
  const output = path.join(buildDir(context), "debug", `${outputBase(root)}-frames`);
  fs.mkdirSync(output, { recursive: true });
  const capture = path.join(output, "montage.bmp");
  const report = path.join(buildDir(context), "debug", `${outputBase(root)}-frame-script-report.json`);
  runCliWithOptions(context, [
    "preview",
    "--root", root,
    "--target", selectedTarget,
    "--build-dir", nativeBuildDirectory,
    "--output", capture,
    "--report", report,
    "--frame-script", selected[0].fsPath,
    "--frame-output-dir", output,
    "--font-budget", config().get("fontBudget", "16x16")
  ], {
    commandName: "frame-script",
    packageRoot: root,
    reportPath: report,
    capture
  });
}

async function openCapture(context) {
  if (lastCapturePath && fs.existsSync(lastCapturePath)) {
    openCaptureFile(lastCapturePath);
    return;
  }
  const selected = await vscode.window.showOpenDialog({
    canSelectFiles: true,
    canSelectFolders: false,
    canSelectMany: false,
    filters: { "JellyFrame captures": ["bmp", "ppm", "png"] },
    openLabel: "Open Capture"
  });
  if (selected && selected[0]) {
    openCaptureFile(selected[0].fsPath);
  }
}

function listBuilds(context) {
  const launcher = debugLauncherPath(context);
  runDetachedPython(context, launcher, ["--list-builds"], { wait: true });
}

function diagnosticSeverity(severity) {
  if (severity === "error") {
    return vscode.DiagnosticSeverity.Error;
  }
  if (severity === "warning") {
    return vscode.DiagnosticSeverity.Warning;
  }
  if (severity === "info") {
    return vscode.DiagnosticSeverity.Information;
  }
  return vscode.DiagnosticSeverity.Warning;
}

function diagnosticRange() {
  return new vscode.Range(new vscode.Position(0, 0), new vscode.Position(0, 1));
}

function updateReportDiagnostics(root) {
  if (!capabilityDiagnostics || !root || !lastReport) {
    return;
  }
  const diagnostics = new Map();
  const addDiagnostic = (filePath, message, severity) => {
    const items = diagnostics.get(filePath) || [];
    items.push(new vscode.Diagnostic(diagnosticRange(), message, severity));
    diagnostics.set(filePath, items);
  };
  const entryPath = path.resolve(root, String(lastReport.app?.entry || "jellyframe.app.json").replace(/^[/\\]/, ""));

  for (const advice of lastReport.developerAdvice || []) {
    const from = advice.source || lastReport.app?.entry || "/jellyframe.app.json";
    const filePath = path.resolve(root, String(from).replace(/^[/\\]/, ""));
    const target = advice.target ? ` [${advice.target}]` : "";
    const text = advice.text ? ` text="${advice.text}"` : "";
    const message = `${advice.title || advice.code || "JellyFrame advice"}${target}${text}: ${advice.action || advice.explanation || ""}`;
    addDiagnostic(filePath, message, diagnosticSeverity(advice.severity));
  }

  for (const warning of lastReport.warnings || []) {
    const from = warning.from || lastReport.app?.entry || "/jellyframe.app.json";
    const filePath = path.resolve(root, String(from).replace(/^[/\\]/, ""));
    const message = warning.message || warning.reason || JSON.stringify(warning);
    addDiagnostic(filePath, message, vscode.DiagnosticSeverity.Warning);
  }

  const pipeline = lastReport.pipelineDiagnostics || {};
  for (const diagnostic of pipeline.diagnostics || []) {
    const stage = diagnostic.stage || "pipeline";
    const code = diagnostic.code || "diagnostic";
    const detail = diagnostic.detail ? ` (${diagnostic.detail})` : "";
    const message = `${stage}::${code}: ${diagnostic.message || "Pipeline diagnostic"}${detail}`;
    addDiagnostic(entryPath, message, diagnosticSeverity(diagnostic.severity));
  }

  capabilityDiagnostics.clear();
  for (const [filePath, items] of diagnostics.entries()) {
    capabilityDiagnostics.set(vscode.Uri.file(filePath), items);
  }
  statusProvider?.refresh();
}

class JellyFrameStatusProvider {
  constructor(context) {
    this.context = context;
    this.changed = new vscode.EventEmitter();
    this.onDidChangeTreeData = this.changed.event;
  }

  refresh() {
    this.changed.fire();
  }

  getTreeItem(element) {
    return element;
  }

  getChildren(element) {
    if (element?.children) {
      return element.children;
    }

    const root = currentPackageRoot();
    const hasPackage = Boolean(root);
    const app = hasPackage ? path.basename(root) : "No package selected";
    const selection = nativeBuildDir(this.context, appRequiresScripting(root));
    const build = selection.buildDirectory || buildDirectoryError(this.context, selection);
    const pipeline = lastReport?.pipelineDiagnostics?.summary;
    const performance = lastReport?.performanceSummary;
    const hasRenderData = Boolean(lastReport?.pipelineDiagnostics?.format)
      || (Array.isArray(lastReport?.responsiveProfiles) && lastReport.responsiveProfiles.length > 0)
      || Boolean(lastReport?.runtimeMetrics)
      || Boolean(lastReport?.portTelemetry);
    const chinese = /^zh(?:-|$)/i.test(vscode.env.language || "");
    const labels = chinese ? {
      currentApp: "当前 App",
      workflow: "工作流",
      reports: "报告与日志",
      environment: "环境",
      device: "设备",
      discoverDevice: "发现设备",
      noDeviceSession: "尚未发现设备",
      connectedDevices: "已连接",
      package: "App 包",
      noPackage: "未识别 App",
      build: "桌面构建",
      validate: "验证 App 包结构",
      check: "检查 App 渲染",
      preview: "预览 App",
      debug: "在 VS Code 中调试",
      debugExternal: "在外部窗口调试",
      playback: "运行程控回放",
      create: "从模板新建 App",
      packageResources: "生成资源包",
      openReport: "打开最近报告",
      openCapture: "打开截图或回放文件",
      showOutput: "查看运行日志",
      reportReady: "报告已生成",
      noReport: "尚未生成报告",
      diagnostics: pipeline
        ? `${pipeline.error || 0} 个错误，${pipeline.warning || 0} 个警告`
        : "运行渲染检查后显示",
      performance: "性能摘要",
      measured: "已测量",
      notMeasured: "尚未测量",
      buildValue: path.basename(build)
    } : {
      currentApp: "Current App",
      workflow: "Workflow",
      reports: "Reports & Logs",
      environment: "Environment",
      device: "Device",
      discoverDevice: "Discover device",
      noDeviceSession: "No device discovered",
      connectedDevices: "Connected",
      package: "App package",
      noPackage: "No App detected",
      build: "Desktop build",
      validate: "Validate App package",
      check: "Check App rendering",
      preview: "Preview App",
      debug: "Debug in VS Code",
      debugExternal: "Debug in external window",
      playback: "Run programmed playback",
      create: "Create App from template",
      packageResources: "Generate resource package",
      openReport: "Open latest report",
      openCapture: "Open capture or playback file",
      showOutput: "View run log",
      reportReady: "Report ready",
      noReport: "No report yet",
      diagnostics: pipeline
        ? `${pipeline.error || 0} errors, ${pipeline.warning || 0} warnings`
        : "Run a render check to populate",
      performance: "Performance summary",
      measured: "Measured",
      notMeasured: "Not measured",
      buildValue: path.basename(build)
    };
    return [
      this.group(labels.currentApp, "package", [
        this.item(hasPackage ? app : labels.noPackage,
          hasPackage ? labels.package : labels.noPackage, root, "jellyframe.debug", "folder-opened"),
      ]),
      this.group(labels.workflow, "rocket", [
        this.item(labels.validate, "", root, "jellyframe.validate", "check"),
        this.item(labels.check, "", root, "jellyframe.check", "check-all"),
        this.item(labels.preview, "", root, "jellyframe.preview", "preview"),
        this.item(labels.debug, "", root, "jellyframe.debug", "debug-alt"),
        this.item(labels.debugExternal, "", root, "jellyframe.debugExternal", "external-link"),
        this.item(labels.playback, "", root, "jellyframe.runFrameScript", "debug-alt"),
        this.item(labels.create, "", undefined, "jellyframe.newFromTemplate", "new-file"),
        this.item(labels.packageResources, "", root, "jellyframe.package", "package"),
      ]),
      this.group(labels.reports, "report", [
        this.item(labels.openReport, lastReport ? labels.reportReady : labels.noReport,
          undefined, "jellyframe.showReport", "output"),
        this.item(labels.openCapture,
          lastCapturePath ? path.basename(lastCapturePath) : labels.noReport,
          lastCapturePath, "jellyframe.openCapture", "open-preview"),
        this.item(labels.showOutput, "", undefined, "jellyframe.showOutput", "output"),
        this.item(chinese ? "管线诊断" : "Pipeline diagnostics", labels.diagnostics),
        this.item(labels.performance, hasRenderData && performance?.rating ? `${labels.measured}: ${performance.rating}` : labels.notMeasured,
          undefined, undefined, "dashboard"),
      ]),
      this.group(labels.environment, "settings-gear", [
        this.item(labels.build, labels.buildValue, build, "jellyframe.listBuilds", "server-environment"),
        this.item(chinese ? "脚本运行时" : "Script runtime",
          appRequiresScripting(root || "") ? (chinese ? "已启用" : "Enabled") : (chinese ? "未启用" : "Not enabled"),
          undefined, undefined, "symbol-event"),
      ]),
      this.group(labels.device, "plug", [
        this.item(labels.discoverDevice, "", undefined, "jellyframe.deviceDiscover", "plug"),
        this.item(labels.connectedDevices,
          Array.isArray(lastDeviceDiscovery)
            ? `${lastDeviceDiscovery.filter((device) => device.connected).length}/${lastDeviceDiscovery.length}`
            : labels.noDeviceSession,
          undefined, undefined, "device-mobile"),
      ]),
    ];
  }

  group(label, icon, children) {
    const item = new vscode.TreeItem(label, vscode.TreeItemCollapsibleState.Expanded);
    item.iconPath = new vscode.ThemeIcon(icon);
    item.children = children;
    item.contextValue = "jellyframe.group";
    return item;
  }

  item(label, description, resource, command, icon) {
    const item = new vscode.TreeItem(label, vscode.TreeItemCollapsibleState.None);
    item.description = description || undefined;
    item.tooltip = resource || description || label;
    item.iconPath = icon ? new vscode.ThemeIcon(icon) : undefined;
    if (resource && fs.existsSync(resource)) {
      item.resourceUri = vscode.Uri.file(resource);
    }
    if (command) {
      item.command = {
        command,
        title: label,
        arguments: resource ? [vscode.Uri.file(resource)] : []
      };
    }
    return item;
  }
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function renderList(items, renderItem, emptyText = "None") {
  if (!items || items.length === 0) {
    return `<p class="muted">${escapeHtml(emptyText)}</p>`;
  }
  return `<ul>${items.map(renderItem).join("")}</ul>`;
}

function reportLabels() {
  const chinese = /^zh(?:-|$)/i.test(vscode.env.language || "");
  return chinese ? {
    panelTitle: "JellyFrame 报告",
    reportTitle: "JellyFrame 报告",
    packageValidationTitle: "JellyFrame 包结构验证",
    target: "目标",
    resources: "资源",
    bytes: "字节",
    packageValid: "包结构有效",
    packageValidationNote: "本报告覆盖包元数据、本地资源、引用和声明的限制；未运行 Render Core，也未测量运行时性能。",
    programmaticValidation: "程控验证",
    script: "脚本",
    status: "状态",
    runtimeLog: "运行日志",
    frameOutputDir: "帧目录",
    montage: "蒙太奇",
    authorAdvice: "作者建议",
    renderingPreflight: "渲染预检",
    rating: "评级",
    score: "评分",
    maxToolTime: "工具耗时上限",
    slowestStage: "最慢阶段",
    maxHeap: "最大堆内存",
    maxFramebuffer: "最大帧缓冲",
    maxDisplayCommands: "最大显示命令数",
    staticEstimate: "静态资源估算",
    staticEstimateNote: "本次操作只检查了包结构和资源预算，未执行 Render Core 布局、帧时序或运行时性能测试。",
    resourceBudget: "资源预算",
    measuredFrameTime: "实测帧时间",
    notAvailable: "不可用",
    pipelineDiagnostics: "管线诊断",
    total: "总数",
    errors: "错误",
    warnings: "警告",
    info: "信息",
    dom: "DOM",
    nodes: "节点",
    layout: "布局",
    boxes: "盒子",
    layers: "层",
    displayCommands: "显示命令",
    estimatedHeap: "估算堆内存",
    warningsSection: "警告",
    resourcesSection: "资源",
    references: "引用",
    path: "路径",
    kind: "类型",
    none: "无",
    missing: "缺失",
    noReport: "请先运行“验证 App 包”“检查 App 渲染”或其他会生成报告的操作。"
  } : {
    panelTitle: "JellyFrame Report",
    reportTitle: "JellyFrame Report",
    packageValidationTitle: "JellyFrame Package Validation",
    target: "Target",
    resources: "Resources",
    bytes: "Bytes",
    packageValid: "Package structure: valid",
    packageValidationNote: "This report covers package metadata, local resources, references and declared limits. It does not run Render Core or measure runtime performance.",
    programmaticValidation: "Programmed Validation",
    script: "Script",
    status: "Status",
    runtimeLog: "Runtime log",
    frameOutputDir: "Frame directory",
    montage: "Montage",
    authorAdvice: "App Author Advice",
    renderingPreflight: "Rendering Preflight",
    rating: "Rating",
    score: "Score",
    maxToolTime: "Max tool time",
    slowestStage: "Slowest stage",
    maxHeap: "Max heap",
    maxFramebuffer: "Max framebuffer",
    maxDisplayCommands: "Max display commands",
    staticEstimate: "Static Resource Estimate",
    staticEstimateNote: "This operation only checked package structure and resource budgets. Render Core layout, frame timing, and runtime performance were not executed.",
    resourceBudget: "Resource budget",
    measuredFrameTime: "Measured frame time",
    notAvailable: "not available",
    pipelineDiagnostics: "Pipeline Diagnostics",
    total: "Total",
    errors: "Errors",
    warnings: "Warnings",
    info: "Info",
    dom: "DOM",
    nodes: "nodes",
    layout: "Layout",
    boxes: "boxes",
    layers: "Layers",
    displayCommands: "Display commands",
    estimatedHeap: "Estimated heap",
    warningsSection: "Warnings",
    resourcesSection: "Resources",
    references: "References",
    path: "Path",
    kind: "Kind",
    none: "None",
    missing: "missing",
    noReport: "Run Validate App Package, Check App Rendering, or another operation that generates a report first."
  };
}

function reportHtml() {
  const report = lastReport;
  const labels = reportLabels();
  const isPackageValidation = lastReportCommand === "validate"
    || report?.reportScope === "package-validation";
  const app = report?.app || {};
  const targetConfig = report?.target || {};
  const warnings = report?.warnings || [];
  const resources = report?.resources || [];
  const references = report?.references || [];
  const developerAdvice = report?.developerAdvice || [];
  const performanceSummary = report?.performanceSummary || {};
  const performanceBottlenecks = performanceSummary?.bottlenecks || [];
  const performanceAdvice = report?.performanceAdvice || [];
  const programmaticValidation = report?.programmaticValidation;
  const pipeline = report?.pipelineDiagnostics || {};
  const summary = pipeline.summary || {};
  const pipelineStats = pipeline.pipeline || {};
  const pipelineDiagnostics = pipeline.diagnostics || [];
  const hasPipelineDiagnostics = Boolean(pipeline.format);
  const hasRenderData = hasPipelineDiagnostics
    || (Array.isArray(report?.responsiveProfiles) && report.responsiveProfiles.length > 0)
    || Boolean(report?.runtimeMetrics)
    || Boolean(report?.portTelemetry);
  return `<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <style>
    body { font-family: var(--vscode-font-family); color: var(--vscode-foreground); padding: 16px; }
    h1, h2 { margin: 0 0 12px; }
    h2 { margin-top: 22px; }
    table { border-collapse: collapse; width: 100%; }
    th, td { border-bottom: 1px solid var(--vscode-panel-border); padding: 6px; text-align: left; }
    code { color: var(--vscode-textPreformat-foreground); }
    .muted { color: var(--vscode-descriptionForeground); }
    .pill { display: inline-block; padding: 1px 6px; border: 1px solid var(--vscode-panel-border); border-radius: 10px; }
    .advice { margin-bottom: 10px; }
    .advice strong { display: block; margin-bottom: 2px; }
    .error { color: var(--vscode-errorForeground); }
    .warning { color: var(--vscode-editorWarning-foreground); }
    .info { color: var(--vscode-editorInfo-foreground); }
  </style>
</head>
<body>
  <h1>${labels.reportTitle}</h1>
  ${report ? `
    <p><strong>${escapeHtml(isPackageValidation ? labels.packageValidationTitle : labels.reportTitle)}</strong></p>
    <p><strong>${escapeHtml(app.name || app.id || "App")}</strong> <span class="muted">${escapeHtml(app.id || "")}</span></p>
    <p>${escapeHtml(labels.target)}: <code>${escapeHtml(targetConfig.id || "default")}</code> · ${escapeHtml(labels.resources)}: ${resources.length} · ${escapeHtml(labels.bytes)}: ${escapeHtml(report.totalResourceBytes || 0)}</p>
    ${isPackageValidation ? `<p class="info"><strong>${escapeHtml(labels.packageValid)}</strong> · ${escapeHtml(labels.packageValidationNote)}</p>` : ""}
    ${programmaticValidation ? `<h2>${escapeHtml(labels.programmaticValidation)}</h2>
      <p>${escapeHtml(labels.status)}: <strong>${escapeHtml(programmaticValidation.status || "unknown")}</strong> · ${escapeHtml(labels.script)}: <code>${escapeHtml(programmaticValidation.frameScript || "")}</code></p>
      <p class="muted">${escapeHtml(labels.runtimeLog)}: <code>${escapeHtml(programmaticValidation.runtimeLog || "")}</code> · ${escapeHtml(labels.frameOutputDir)}: <code>${escapeHtml(programmaticValidation.frameOutputDir || "")}</code> · ${escapeHtml(labels.montage)}: <code>${escapeHtml(programmaticValidation.montage || "")}</code></p>` : ""}
    ${developerAdvice.length ? `<h2>${escapeHtml(labels.authorAdvice)}</h2>
    ${renderList(developerAdvice, (advice) => `<li class="advice"><strong><span class="pill ${escapeHtml(advice.severity || "")}">${escapeHtml(advice.severity || "advice")}</span> ${escapeHtml(advice.title || advice.code || "Review item")}${advice.target ? ` <span class="muted">[${escapeHtml(advice.target)}]</span>` : ""}</strong><span>${escapeHtml(advice.action || advice.explanation || "")}</span>${advice.recipe ? ` <span class="muted">Recipe: <code>${escapeHtml(advice.recipe)}</code></span>` : ""}${advice.text ? ` <span class="muted">Text: <code>${escapeHtml(advice.text)}</code></span>` : ""}${advice.path ? ` <span class="muted">Path: <code>${escapeHtml(advice.path)}</code></span>` : ""}${advice.node ? ` <span class="muted">Node: <code>${escapeHtml(advice.node)}</code></span>` : ""}${advice.metrics ? ` <span class="muted">Metrics: <code>${escapeHtml(JSON.stringify(advice.metrics))}</code></span>` : ""}</li>`, labels.none)}
    ` : ""}
    ${!isPackageValidation && hasRenderData && performanceSummary.model ? `<h2>${escapeHtml(labels.renderingPreflight)}</h2>
      <p>
        ${escapeHtml(labels.rating)}: <strong>${escapeHtml(performanceSummary.rating || "unknown")}</strong>
        · ${escapeHtml(labels.score)}: ${escapeHtml(performanceSummary.score || 0)}
        · ${escapeHtml(labels.maxToolTime)}: ${escapeHtml(performanceSummary.maxTotalPipelineUs || 0)} us
        · ${escapeHtml(labels.slowestStage)}: ${escapeHtml((performanceSummary.slowestMeasuredStage || {}).stage || "n/a")}
        · ${escapeHtml(labels.maxHeap)}: ${escapeHtml(performanceSummary.maxEstimatedHeapBytes || 0)} bytes
        · ${escapeHtml(labels.maxFramebuffer)}: ${escapeHtml(performanceSummary.maxFramebufferBytes || 0)} bytes
        · ${escapeHtml(labels.maxDisplayCommands)}: ${escapeHtml(performanceSummary.maxDisplayCommands || 0)}
      </p>
      ${renderList(performanceBottlenecks, (item) => `<li class="advice"><strong>${escapeHtml(item.title || item.code || "Performance bottleneck")}${item.target ? ` <span class="muted">[${escapeHtml(item.target)}]</span>` : ""}</strong>${item.metrics ? ` <span class="muted">Metrics: <code>${escapeHtml(JSON.stringify(item.metrics))}</code></span>` : ""}</li>`, labels.none)}
      ${renderList(performanceAdvice, (advice) => `<li class="advice"><strong><span class="pill ${escapeHtml(advice.severity || "")}">${escapeHtml(advice.severity || "advice")}</span> ${escapeHtml(advice.title || advice.code || "Performance item")}${advice.target ? ` <span class="muted">[${escapeHtml(advice.target)}]</span>` : ""}</strong><span>${escapeHtml(advice.action || advice.explanation || "")}</span>${advice.metrics ? ` <span class="muted">Metrics: <code>${escapeHtml(JSON.stringify(advice.metrics))}</code></span>` : ""}</li>`, labels.none)}
    ` : !isPackageValidation && performanceSummary.source === "package-preflight-estimate" ? `<h2>${escapeHtml(labels.staticEstimate)}</h2>
      <p class="muted">${escapeHtml(labels.staticEstimateNote)}</p>
      <p>${escapeHtml(labels.resourceBudget)}: ${escapeHtml(performanceSummary.resourceBudgetPercent || 0)}% · ${escapeHtml(labels.measuredFrameTime)}: ${escapeHtml(labels.notAvailable)}</p>` : ""}
    ${hasPipelineDiagnostics ? `<h2>${escapeHtml(labels.pipelineDiagnostics)}</h2>
      <p>
        ${escapeHtml(labels.total)}: ${escapeHtml(summary.total || 0)}
        · <span class="error">${escapeHtml(labels.errors)}: ${escapeHtml(summary.error || 0)}</span>
        · <span class="warning">${escapeHtml(labels.warnings)}: ${escapeHtml(summary.warning || 0)}</span>
        · <span class="info">${escapeHtml(labels.info)}: ${escapeHtml(summary.info || 0)}</span>
      </p>
      <p class="muted">
        ${escapeHtml(labels.dom)}: ${escapeHtml(pipelineStats.domNodes || 0)} ${escapeHtml(labels.nodes)} ·
        ${escapeHtml(labels.layout)}: ${escapeHtml(pipelineStats.layoutBoxes || 0)} ${escapeHtml(labels.boxes)} ·
        ${escapeHtml(labels.layers)}: ${escapeHtml(pipelineStats.layers || 0)} ·
        ${escapeHtml(labels.displayCommands)}: ${escapeHtml(pipelineStats.displayCommands || 0)} ·
        ${escapeHtml(labels.estimatedHeap)}: ${escapeHtml(pipelineStats.estimatedHeapBytes || 0)} bytes
      </p>
      ${renderList(pipelineDiagnostics, (diagnostic) => `<li><span class="pill ${escapeHtml(diagnostic.severity || "")}">${escapeHtml(diagnostic.severity || "diagnostic")}</span> <code>${escapeHtml(diagnostic.stage || "pipeline")}::${escapeHtml(diagnostic.code || "diagnostic")}</code> · ${escapeHtml(diagnostic.message || "")}${diagnostic.detail ? ` <span class="muted">(${escapeHtml(diagnostic.detail)})</span>` : ""}</li>`, labels.none)}
    ` : ""}
    <h2>${escapeHtml(labels.warningsSection)}</h2>
    ${renderList(warnings, (warning) => `<li>${escapeHtml(warning.message || warning.reason || JSON.stringify(warning))}</li>`, labels.none)}
    <h2>${escapeHtml(labels.resourcesSection)}</h2>
    <table><tr><th>${escapeHtml(labels.path)}</th><th>${escapeHtml(labels.kind)}</th><th>${escapeHtml(labels.bytes)}</th></tr>
    ${resources.map((resource) => `<tr><td><code>${escapeHtml(resource.path)}</code></td><td>${escapeHtml(resource.kind)}</td><td>${escapeHtml(resource.size)}</td></tr>`).join("")}
    </table>
    <h2>${escapeHtml(labels.references)}</h2>
    ${renderList(references, (reference) => `<li><code>${escapeHtml(reference.from)}</code> -> <code>${escapeHtml(reference.value)}</code> <span class="muted">${escapeHtml(reference.kind)} ${reference.packaged === false ? escapeHtml(labels.missing) : ""}</span></li>`, labels.none)}
  ` : `<p class="muted">${escapeHtml(labels.noReport)}</p>`}
</body>
</html>`;
}

function showReportPanel(context) {
  if (!reportPanel) {
    reportPanel = vscode.window.createWebviewPanel(
      "jellyframeReport",
      reportLabels().panelTitle,
      vscode.ViewColumn.Beside,
      { enableScripts: false }
    );
    reportPanel.onDidDispose(() => {
      reportPanel = undefined;
    }, null, context.subscriptions);
  }
  reportPanel.webview.html = reportHtml();
  reportPanel.reveal(vscode.ViewColumn.Beside);
}

function templateNames(context) {
  const root = path.join(repoRoot(context), "tools", "templates", "apps");
  if (!fs.existsSync(root)) {
    return [];
  }
  return fs.readdirSync(root).filter((name) => fs.statSync(path.join(root, name)).isDirectory()).sort();
}

async function newFromTemplate(context) {
  const picked = await vscode.window.showQuickPick(templateNames(context), {
    placeHolder: "Select JellyFrame app template"
  });
  if (!picked) {
    return;
  }
  const workspace = workspaceFolderPath() || repoRoot(context);
  const output = await vscode.window.showInputBox({
    prompt: "Output directory",
    value: path.join(workspace, picked)
  });
  if (!output) {
    return;
  }
  const appId = await vscode.window.showInputBox({
    prompt: "Manifest app id",
    value: `org.example.${picked}`
  });
  if (!appId) {
    return;
  }
  const name = await vscode.window.showInputBox({
    prompt: "Manifest app name",
    value: picked.charAt(0).toUpperCase() + picked.slice(1)
  });
  if (!name) {
    return;
  }
  const selectedTarget = await target();
  if (!selectedTarget) {
    return;
  }
  runCli(context, [
    "new",
    "--template",
    picked,
    "--output",
    output,
    "--id",
    appId,
    "--name",
    name,
    "--target",
    selectedTarget
  ]);
}

function activate(context) {
  capabilityDiagnostics = vscode.languages.createDiagnosticCollection("jellyframe");
  statusProvider = new JellyFrameStatusProvider(context);
  context.subscriptions.push(
    capabilityDiagnostics,
    statusProvider.changed,
    vscode.window.registerTreeDataProvider("jellyframe.status", statusProvider),
    vscode.window.onDidChangeActiveTextEditor(() => statusProvider?.refresh()),
    vscode.workspace.onDidChangeWorkspaceFolders(() => statusProvider?.refresh()),
    vscode.workspace.onDidSaveTextDocument(() => statusProvider?.refresh()),
    vscode.commands.registerCommand("jellyframe.validate", (resourceUri) => runPackageCommand(context, "validate", resourceUri)),
    vscode.commands.registerCommand("jellyframe.check", (resourceUri) => runPackageCommand(context, "check", resourceUri)),
    vscode.commands.registerCommand("jellyframe.preview", (resourceUri) => previewPackage(context, resourceUri)),
    vscode.commands.registerCommand("jellyframe.debug", (resourceUri) => debugApp(context, resourceUri)),
    vscode.commands.registerCommand("jellyframe.debugExternal", (resourceUri) => debugExternalApp(context, resourceUri)),
    vscode.commands.registerCommand("jellyframe.runFrameScript", (resourceUri) => runFrameScript(context, resourceUri)),
    vscode.commands.registerCommand("jellyframe.openCapture", () => openCapture(context)),
    vscode.commands.registerCommand("jellyframe.listBuilds", () => listBuilds(context)),
    vscode.commands.registerCommand("jellyframe.package", (resourceUri) => runPackageCommand(context, "package", resourceUri)),
    vscode.commands.registerCommand("jellyframe.newFromTemplate", () => newFromTemplate(context)),
    vscode.commands.registerCommand("jellyframe.showReport", () => showReportPanel(context)),
    vscode.commands.registerCommand("jellyframe.showOutput", () => showOutputChannel()),
    vscode.commands.registerCommand("jellyframe.deviceDiscover", () => discoverDevice(context))
  );
}

function deactivate() {
  if (outputChannel) {
    outputChannel.dispose();
    outputChannel = undefined;
  }
  if (capabilityDiagnostics) {
    capabilityDiagnostics.dispose();
    capabilityDiagnostics = undefined;
  }
  if (statusProvider) {
    statusProvider.changed.dispose();
    statusProvider = undefined;
  }
}

module.exports = {
  activate,
  deactivate
};
