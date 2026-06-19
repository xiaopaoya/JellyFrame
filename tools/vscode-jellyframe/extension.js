const fs = require("fs");
const path = require("path");
const childProcess = require("child_process");
const vscode = require("vscode");

let outputChannel;
let reportPanel;
let capabilityDiagnostics;
let lastReport;

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
  return path.join(repoRoot(context), "build", "Release");
}

function exeName(name) {
  return process.platform === "win32" ? `${name}.exe` : name;
}

function nativeToolPath(context, name) {
  return path.join(nativeBuildDir(context), exeName(name));
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
  channel.appendLine(`+ ${[python, ...commandArgs].join(" ")}`);
  const child = childProcess.spawn(python, commandArgs, {
    cwd: repoRoot(context),
    shell: false
  });
  child.stdout.on("data", (chunk) => {
    const text = chunk.toString();
    channel.append(text);
  });
  child.stderr.on("data", (chunk) => {
    const text = chunk.toString();
    channel.append(text);
  });
  child.on("error", (error) => {
    channel.appendLine(`JellyFrame command failed to start: ${error.message}`);
    vscode.window.showErrorMessage(`JellyFrame command failed to start: ${error.message}`);
  });
  child.on("close", (code) => {
    channel.appendLine(`JellyFrame command exited with code ${code}`);
    if (options.reportPath) {
      loadReport(options.reportPath);
    }
    if (options.packageRoot && options.reportPath) {
      updateReportDiagnostics(options.packageRoot);
      showReportPanel(context);
    }
    if (code !== 0) {
      vscode.window.showErrorMessage(`JellyFrame command failed with code ${code}`);
    }
  });
}

function loadReport(reportPath) {
  try {
    if (fs.existsSync(reportPath)) {
      lastReport = JSON.parse(fs.readFileSync(reportPath, "utf8"));
    }
  } catch (error) {
    ensureOutputChannel().appendLine(`failed to read report ${reportPath}: ${error.message}`);
  }
}

function runDetachedTool(context, executable, args) {
  const channel = ensureOutputChannel();
  channel.appendLine(`+ ${[executable, ...args].join(" ")}`);
  const child = childProcess.spawn(executable, args, {
    cwd: repoRoot(context),
    detached: true,
    stdio: "ignore",
    windowsHide: false
  });
  child.on("error", (error) => {
    channel.appendLine(`JellyFrame tool failed to start: ${error.message}`);
    vscode.window.showErrorMessage(`JellyFrame tool failed to start: ${error.message}`);
  });
  child.unref();
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

async function packageRoot() {
  const active = vscode.window.activeTextEditor?.document.uri.fsPath;
  if (active) {
    const found = findPackageRootFrom(active);
    if (found) {
      return found;
    }
  }
  const workspace = workspaceFolderPath();
  if (workspace) {
    const found = findPackageRootFrom(workspace);
    if (found) {
      return found;
    }
  }
  const selected = await vscode.window.showOpenDialog({
    canSelectFiles: false,
    canSelectFolders: true,
    canSelectMany: false,
    openLabel: "Select JellyFrame package root"
  });
  return selected && selected[0] ? selected[0].fsPath : undefined;
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

async function runPackageCommand(context, commandName) {
  const root = await packageRoot();
  if (!root) {
    return;
  }
  const selectedTarget = await target();
  if (!selectedTarget) {
    return;
  }
  ensureBuildDir(context);
  const base = outputBase(root);
  const report = path.join(buildDir(context), `vscode-${base}-report.json`);
  const args = [commandName, "--root", root, "--target", selectedTarget, "--report", report];
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
  runCliWithOptions(context, args, {
    commandName,
    packageRoot: root,
    reportPath: report
  });
}

async function previewPackage(context) {
  const root = await packageRoot();
  if (!root) {
    return;
  }
  const selectedTarget = await target();
  if (!selectedTarget) {
    return;
  }
  ensureBuildDir(context);
  const base = outputBase(root);
  const output = path.join(buildDir(context), `vscode-${base}.ppm`);
  const report = path.join(buildDir(context), `vscode-${base}-preview-report.json`);
  runCliWithOptions(
    context,
    ["preview", "--root", root, "--target", selectedTarget, "--output", output, "--report", report],
    {
      commandName: "preview",
      packageRoot: root,
      reportPath: report
    }
  );
}

async function openWin32Browser(context) {
  if (process.platform !== "win32") {
    vscode.window.showErrorMessage("JellyFrame Win32 browser is only available on Windows.");
    return;
  }
  const root = await packageRoot();
  if (!root) {
    return;
  }
  const executable = nativeToolPath(context, "jellyframe_win32_browser");
  if (!fs.existsSync(executable)) {
    vscode.window.showErrorMessage(`Missing Win32 browser: ${executable}`);
    return;
  }
  runDetachedTool(context, executable, ["--app", root]);
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
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function renderList(items, renderItem) {
  if (!items || items.length === 0) {
    return "<p class=\"muted\">None</p>";
  }
  return `<ul>${items.map(renderItem).join("")}</ul>`;
}

function reportHtml() {
  const report = lastReport;
  const app = report?.app || {};
  const targetConfig = report?.target || {};
  const warnings = report?.warnings || [];
  const resources = report?.resources || [];
  const references = report?.references || [];
  const pipeline = report?.pipelineDiagnostics || {};
  const summary = pipeline.summary || {};
  const pipelineStats = pipeline.pipeline || {};
  const pipelineDiagnostics = pipeline.diagnostics || [];
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
    .error { color: var(--vscode-errorForeground); }
    .warning { color: var(--vscode-editorWarning-foreground); }
    .info { color: var(--vscode-editorInfo-foreground); }
  </style>
</head>
<body>
  <h1>JellyFrame Report</h1>
  ${report ? `
    <p><strong>${escapeHtml(app.name || app.id || "App")}</strong> <span class="muted">${escapeHtml(app.id || "")}</span></p>
    <p>Target: <code>${escapeHtml(targetConfig.id || "default")}</code> · Resources: ${resources.length} · Bytes: ${escapeHtml(report.totalResourceBytes || 0)}</p>
    <h2>Pipeline Diagnostics</h2>
    ${pipeline.format ? `
      <p>
        Total: ${escapeHtml(summary.total || 0)}
        · <span class="error">Errors: ${escapeHtml(summary.error || 0)}</span>
        · <span class="warning">Warnings: ${escapeHtml(summary.warning || 0)}</span>
        · <span class="info">Info: ${escapeHtml(summary.info || 0)}</span>
      </p>
      <p class="muted">
        DOM: ${escapeHtml(pipelineStats.domNodes || 0)} nodes ·
        Layout: ${escapeHtml(pipelineStats.layoutBoxes || 0)} boxes ·
        Layers: ${escapeHtml(pipelineStats.layers || 0)} ·
        Display commands: ${escapeHtml(pipelineStats.displayCommands || 0)} ·
        Estimated heap: ${escapeHtml(pipelineStats.estimatedHeapBytes || 0)} bytes
      </p>
      ${renderList(pipelineDiagnostics, (diagnostic) => `<li><span class="pill ${escapeHtml(diagnostic.severity || "")}">${escapeHtml(diagnostic.severity || "diagnostic")}</span> <code>${escapeHtml(diagnostic.stage || "pipeline")}::${escapeHtml(diagnostic.code || "diagnostic")}</code> · ${escapeHtml(diagnostic.message || "")}${diagnostic.detail ? ` <span class="muted">(${escapeHtml(diagnostic.detail)})</span>` : ""}</li>`)}
    ` : "<p class=\"muted\">No pipeline diagnostics in the latest report.</p>"}
    <h2>Warnings</h2>
    ${renderList(warnings, (warning) => `<li>${escapeHtml(warning.message || warning.reason || JSON.stringify(warning))}</li>`)}
    <h2>Resources</h2>
    <table><tr><th>Path</th><th>Kind</th><th>Bytes</th></tr>
    ${resources.map((resource) => `<tr><td><code>${escapeHtml(resource.path)}</code></td><td>${escapeHtml(resource.kind)}</td><td>${escapeHtml(resource.size)}</td></tr>`).join("")}
    </table>
    <h2>References</h2>
    ${renderList(references, (reference) => `<li><code>${escapeHtml(reference.from)}</code> -> <code>${escapeHtml(reference.value)}</code> <span class="muted">${escapeHtml(reference.kind)} ${reference.packaged === false ? "missing" : ""}</span></li>`)}
  ` : "<p class=\"muted\">Run JellyFrame: Validate Package or JellyFrame: Check Package Capabilities first.</p>"}
</body>
</html>`;
}

function showReportPanel(context) {
  if (!reportPanel) {
    reportPanel = vscode.window.createWebviewPanel(
      "jellyframeReport",
      "JellyFrame Report",
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
  context.subscriptions.push(
    capabilityDiagnostics,
    vscode.commands.registerCommand("jellyframe.validate", () => runPackageCommand(context, "validate")),
    vscode.commands.registerCommand("jellyframe.check", () => runPackageCommand(context, "check")),
    vscode.commands.registerCommand("jellyframe.preview", () => previewPackage(context)),
    vscode.commands.registerCommand("jellyframe.openWin32Browser", () => openWin32Browser(context)),
    vscode.commands.registerCommand("jellyframe.package", () => runPackageCommand(context, "package")),
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
}

module.exports = {
  activate,
  deactivate
};
