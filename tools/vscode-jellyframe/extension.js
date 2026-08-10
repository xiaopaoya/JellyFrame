const fs = require("fs");
const path = require("path");
const childProcess = require("child_process");
const vscode = require("vscode");

let outputChannel;
let reportPanel;
let capabilityDiagnostics;
let lastReport;
let lastReportCommand;
let lastPackageRoot;
let lastCapturePath;
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

function nativeBuildDir(context) {
  const configured = config().get("buildDir", "").trim();
  if (configured) {
    return path.isAbsolute(configured) ? configured : path.resolve(repoRoot(context), configured);
  }
  for (const candidate of [
    path.join(repoRoot(context), "build", "Release"),
    path.join(repoRoot(context), "build", "Debug"),
    path.join(repoRoot(context), "build-script", "Release")
  ]) {
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }
  return path.join(repoRoot(context), "build", "Release");
}

function debugLauncherPath(context) {
  return path.join(repoRoot(context), "tools", "debug", "jellyframe_debug.py");
}

function ensureOutputChannel() {
  if (!outputChannel) {
    outputChannel = vscode.window.createOutputChannel("JellyFrame");
  }
  outputChannel.show(true);
  return outputChannel;
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
  child.stdout.on("data", (chunk) => {
    const text = chunk.toString();
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
  let current = fs.statSync(startPath).isDirectory() ? startPath : path.dirname(startPath);
  while (true) {
    if (fs.existsSync(path.join(current, "jellyframe.app.json"))) {
      return current;
    }
    const parent = path.dirname(current);
    if (parent === current) {
      return undefined;
    }
    current = parent;
  }
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
  const selectedTarget = await target();
  if (!selectedTarget) {
    return;
  }
  ensureBuildDir(context);
  const base = outputBase(root);
  const report = path.join(buildDir(context), `vscode-${base}-${commandName}-report.json`);
  const args = [commandName, "--root", root, "--target", selectedTarget, "--report", report];
  const options = {
    commandName,
    packageRoot: root,
    reportPath: report
  };
  if (commandName === "validate") {
    const frameScript = await selectFrameScript(root, /^zh(?:-|$)/i.test(vscode.env.language || "")
      ? "选择验证方式"
      : "Choose validation mode");
    if (frameScript) {
      const frameOutputDir = path.join(buildDir(context), "debug", `${base}-validate-frames`);
      const montage = path.join(buildDir(context), "debug", `${base}-validate-montage.bmp`);
      args.push(
        "--build-dir", nativeBuildDir(context),
        "--frame-script", frameScript,
        "--frame-output-dir", frameOutputDir,
        "--frame-montage", montage
      );
      options.capture = montage;
    }
  }
  if (commandName === "check") {
    args.push("--font-budget", config().get("fontBudget", "16x16"));
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
  ensureBuildDir(context);
  const base = outputBase(root);
  const runtimeLog = path.join(buildDir(context), `vscode-${base}-debug-runtime.log`);
  const report = path.join(buildDir(context), `vscode-${base}-debug-report.json`);
  runDetachedPython(context, launcher, [
    "--build-dir", nativeBuildDir(context),
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
        "--build-dir", nativeBuildDir(context),
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
  return `<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';">
  <style>
    body { margin: 0; min-height: 100vh; color: var(--vscode-foreground); background: var(--vscode-editor-background); font-family: var(--vscode-font-family); display: grid; grid-template-rows: auto minmax(0, 1fr); }
    header { display: flex; align-items: center; gap: 12px; padding: 8px 12px; border-bottom: 1px solid var(--vscode-panel-border); font-size: 12px; }
    #status { color: var(--vscode-descriptionForeground); flex: 1; }
    button { appearance: none; min-width: 28px; min-height: 26px; border: 1px solid var(--vscode-button-border, transparent); color: var(--vscode-button-foreground); background: var(--vscode-button-background); cursor: pointer; }
    button:hover { background: var(--vscode-button-hoverBackground); }
    #stage { min-height: 0; display: grid; place-items: center; padding: 14px; overflow: hidden; }
    #frame { display: block; max-width: 100%; max-height: 100%; object-fit: contain; user-select: none; -webkit-user-drag: none; outline: none; background: #111; }
    #empty { color: var(--vscode-descriptionForeground); }
  </style>
</head>
<body>
  <header><strong>JellyFrame</strong><span id="status">Starting desktop shell...</span><button id="stop" title="Stop desktop shell">Stop</button></header>
  <main id="stage"><span id="empty">Waiting for the first frame...</span><img id="frame" tabindex="0" hidden alt="JellyFrame app frame"></main>
  <script nonce="${nonce}">
    const vscode = acquireVsCodeApi();
    const frame = document.getElementById('frame');
    const empty = document.getElementById('empty');
    const status = document.getElementById('status');
    const stop = document.getElementById('stop');
    let viewport = { width: 1, height: 1 };
    let latestSequence = 0;
    let moveQueued = false;
    let pendingMove = null;
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
      const p = point(event);
      const delta = Math.max(-120, Math.min(120, Math.round(-event.deltaY)));
      vscode.postMessage({ type: 'input', line: 'wheel ' + p.x + ' ' + p.y + ' ' + delta });
      event.preventDefault();
    }, { passive: false });
    frame.addEventListener('keydown', (event) => {
      const keys = { Escape: 'escape', Enter: 'enter', ' ': 'space', Tab: 'tab', ArrowUp: 'up', ArrowDown: 'down', Backspace: 'backspace' };
      if (keys[event.key]) { vscode.postMessage({ type: 'input', line: 'key ' + keys[event.key] }); event.preventDefault(); }
    });
    stop.addEventListener('click', () => vscode.postMessage({ type: 'stop' }));
    window.addEventListener('message', (event) => {
      const message = event.data;
      if (message.type === 'frame' && message.sequence > latestSequence) {
        latestSequence = message.sequence;
        viewport = { width: message.width, height: message.height };
        frame.src = message.dataUri;
        frame.hidden = false;
        empty.hidden = true;
        status.textContent = 'Frame ' + message.sequence + ' · ' + message.width + 'x' + message.height;
      } else if (message.type === 'status') {
        status.textContent = message.text;
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
  try {
    session.panel.webview.postMessage({ type: 'status', text: reason || 'Stopping desktop shell...' });
  } catch (_) {
    // The panel may already be disposing; the process still needs to be stopped.
  }
  ensureOutputChannel().appendLine(`[embedded] stop requested: ${reason || 'user request'}`);
  if (session.child.stdin?.writable) {
    session.child.stdin.write('quit\n');
    session.child.stdin.end();
  }
  session.forceStopTimer = setTimeout(() => {
    if (!session.exited) {
      ensureOutputChannel().appendLine(`[embedded] shell did not exit in time; terminating process tree pid=${session.child.pid}`);
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

async function deliverEmbeddedFrame(session, frame) {
  if (!session.active || frame.sequence < session.latestAnnouncedSequence) {
    return;
  }
  try {
    const bytes = await fs.promises.readFile(frame.path);
    if (!session.active || frame.sequence !== session.latestAnnouncedSequence || frame.sequence <= session.lastDeliveredSequence) {
      return;
    }
    session.lastDeliveredSequence = frame.sequence;
    session.panel.webview.postMessage({
      type: 'frame',
      sequence: frame.sequence,
      width: frame.width,
      height: frame.height,
      dataUri: `data:image/bmp;base64,${bytes.toString('base64')}`
    });
  } catch (error) {
    ensureOutputChannel().appendLine(`[embedded] failed to read frame ${frame.sequence}: ${error.message}`);
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
  const args = [launcher, '--build-dir', nativeBuildDir(context), '--app', root, '--vscode-debug', '--vscode-frame-dir', frameDir, '--wait'];
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
    frameDir,
    latestAnnouncedSequence: 0,
    lastDeliveredSequence: 0,
    outputBuffer: '',
    forceStopTimer: undefined
  };
  embeddedDebugSession = session;
  child.stdout.on('data', (chunk) => {
    session.outputBuffer += chunk.toString();
    let newline = 0;
    while ((newline = session.outputBuffer.indexOf('\n')) >= 0) {
      const line = session.outputBuffer.slice(0, newline).replace(/\r$/, '');
      session.outputBuffer = session.outputBuffer.slice(newline + 1);
      const frame = parseEmbeddedFrameLine(line);
      if (frame) {
        session.latestAnnouncedSequence = Math.max(session.latestAnnouncedSequence, frame.sequence);
        void deliverEmbeddedFrame(session, frame);
      } else if (line) {
        channel.appendLine(`[embedded] ${line}`);
      }
    }
  });
  child.stderr.on('data', (chunk) => channel.append(`[embedded] ${chunk.toString()}`));
  child.on('error', (error) => {
    channel.appendLine(`[embedded] failed to start: ${error.message}`);
    panel.webview.postMessage({ type: 'status', text: `Failed to start: ${error.message}` });
  });
  child.on('close', (code) => {
    session.exited = true;
    session.active = false;
    if (session.forceStopTimer) {
      clearTimeout(session.forceStopTimer);
    }
    session.resolveExit?.();
    channel.appendLine(`[embedded] shell exited with code ${code}`);
    panel.webview.postMessage({ type: 'status', text: `Desktop shell stopped (exit ${code ?? 'unknown'}).` });
    if (embeddedDebugSession === session) {
      embeddedDebugSession = undefined;
    }
    setTimeout(() => fs.rm(frameDir, { recursive: true, force: true }, () => {}), 250);
  });
  panel.onDidReceiveMessage((message) => {
    if (message?.type === 'stop') {
      stopEmbeddedDebugSession(session, 'Stopping desktop shell...');
    } else if (message?.type === 'input' && typeof message.line === 'string' && /^[a-z]+(?: [a-z-]+)?(?: -?\d+){0,4}$/.test(message.line)) {
      if (session.active && !session.stopping && child.stdin.writable) {
        child.stdin.write(`${message.line}\n`);
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
  const output = path.join(buildDir(context), "debug", `${outputBase(root)}-frames`);
  fs.mkdirSync(output, { recursive: true });
  const capture = path.join(output, "montage.bmp");
  const report = path.join(buildDir(context), "debug", `${outputBase(root)}-frame-script-report.json`);
  runCliWithOptions(context, [
    "preview",
    "--root", root,
    "--target", selectedTarget,
    "--build-dir", nativeBuildDir(context),
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

  getChildren() {
    const root = lastPackageRoot || "No package selected";
    const build = nativeBuildDir(this.context);
    const report = lastReport ? "Loaded report" : "No report loaded";
    const pipeline = lastReport?.pipelineDiagnostics?.summary;
    const performance = lastReport?.performanceSummary;
    const isPackageValidation = lastReportCommand === "validate"
      || lastReport?.reportScope === "package-validation";
    const hasRenderData = Boolean(lastReport?.pipelineDiagnostics?.format)
      || (Array.isArray(lastReport?.responsiveProfiles) && lastReport.responsiveProfiles.length > 0)
      || Boolean(lastReport?.runtimeMetrics)
      || Boolean(lastReport?.portTelemetry);
    const diagnostics = pipeline
      ? `Diagnostics: ${pipeline.error || 0} errors, ${pipeline.warning || 0} warnings`
      : "Diagnostics: run Check or Preview";
    const perf = isPackageValidation
      ? "Performance: not part of package validation"
      : hasRenderData && performance?.rating
      ? `Performance: ${performance.rating} (${performance.score || 0})`
      : performance?.source === "package-preflight-estimate"
        ? "Performance: static estimate only"
      : "Performance: not measured";
    return [
      this.item(`App: ${path.basename(root)}`, root, "jellyframe.debug"),
      this.item(`Build: ${build}`, build, "jellyframe.listBuilds"),
      this.item(report, undefined, "jellyframe.showReport"),
      this.item(diagnostics),
      this.item(perf),
      this.item(lastCapturePath ? `Capture: ${path.basename(lastCapturePath)}` : "Capture: open or run a frame script", lastCapturePath, "jellyframe.openCapture")
    ];
  }

  item(label, resource, command) {
    const item = new vscode.TreeItem(label, vscode.TreeItemCollapsibleState.None);
    item.tooltip = resource || label;
    if (resource && fs.existsSync(resource)) {
      item.resourceUri = vscode.Uri.file(resource);
    }
    if (command) {
      item.command = { command, title: label };
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
    vscode.commands.registerCommand("jellyframe.showReport", () => showReportPanel(context))
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
