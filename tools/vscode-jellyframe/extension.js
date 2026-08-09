const fs = require("fs");
const path = require("path");
const childProcess = require("child_process");
const vscode = require("vscode");

let outputChannel;
let reportPanel;
let capabilityDiagnostics;
let lastReport;
let lastPackageRoot;
let lastCapturePath;
let statusProvider;

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
    if (code === 0 && options.reportPath) {
      loadReport(options.reportPath);
    }
    if (code === 0 && options.packageRoot && options.reportPath) {
      updateReportDiagnostics(options.packageRoot);
      showReportPanel(context);
    }
    if (code === 0 && options.capture && config().get("openCaptureAfterRun", true)) {
      openCaptureFile(options.capture);
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

async function previewPackage(context, resourceUri) {
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
  const output = path.join(buildDir(context), `vscode-${base}.ppm`);
  const report = path.join(buildDir(context), `vscode-${base}-preview-report.json`);
  runCliWithOptions(
    context,
    ["preview", "--root", root, "--target", selectedTarget, "--output", output, "--report", report],
    {
      commandName: "preview",
      packageRoot: root,
      reportPath: report,
      capture: output
    }
  );
}

async function debugApp(context, resourceUri) {
  if (process.platform !== "win32") {
    vscode.window.showErrorMessage("JellyFrame desktop shell is only available on Windows.");
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
  runDetachedPython(context, launcher, [
    "--build-dir", nativeBuildDir(context),
    "--app", root,
    "--wait"
  ], { wait: true });
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
  ensureBuildDir(context);
  const output = path.join(buildDir(context), "debug", `${outputBase(root)}-frames`);
  fs.mkdirSync(output, { recursive: true });
  const capture = path.join(output, "montage.bmp");
  const launcher = debugLauncherPath(context);
  runDetachedPython(context, launcher, [
    "--build-dir", nativeBuildDir(context),
    "--app", root,
    "--frame-script", selected[0].fsPath,
    "--wait",
    "--", "--capture-frames", output, "--capture-montage", capture
  ], { wait: true, capture });
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
    const diagnostics = pipeline
      ? `Diagnostics: ${pipeline.error || 0} errors, ${pipeline.warning || 0} warnings`
      : "Diagnostics: run Check or Preview";
    const perf = performance?.rating
      ? `Performance: ${performance.rating} (${performance.score || 0})`
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
  const developerAdvice = report?.developerAdvice || [];
  const performanceSummary = report?.performanceSummary || {};
  const performanceBottlenecks = performanceSummary?.bottlenecks || [];
  const performanceAdvice = report?.performanceAdvice || [];
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
    .advice { margin-bottom: 10px; }
    .advice strong { display: block; margin-bottom: 2px; }
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
    <h2>App Author Advice</h2>
    ${renderList(developerAdvice, (advice) => `<li class="advice"><strong><span class="pill ${escapeHtml(advice.severity || "")}">${escapeHtml(advice.severity || "advice")}</span> ${escapeHtml(advice.title || advice.code || "Review item")}${advice.target ? ` <span class="muted">[${escapeHtml(advice.target)}]</span>` : ""}</strong><span>${escapeHtml(advice.action || advice.explanation || "")}</span>${advice.recipe ? ` <span class="muted">Recipe: <code>${escapeHtml(advice.recipe)}</code></span>` : ""}${advice.text ? ` <span class="muted">Text: <code>${escapeHtml(advice.text)}</code></span>` : ""}${advice.path ? ` <span class="muted">Path: <code>${escapeHtml(advice.path)}</code></span>` : ""}${advice.node ? ` <span class="muted">Node: <code>${escapeHtml(advice.node)}</code></span>` : ""}${advice.metrics ? ` <span class="muted">Metrics: <code>${escapeHtml(JSON.stringify(advice.metrics))}</code></span>` : ""}</li>`)}
    <h2>Performance</h2>
    ${performanceSummary.model ? `
      <p>
        Rating: <strong>${escapeHtml(performanceSummary.rating || "unknown")}</strong>
        · Score: ${escapeHtml(performanceSummary.score || 0)}
        · Max tool time: ${escapeHtml(performanceSummary.maxTotalPipelineUs || 0)} us
        · Slowest stage: ${escapeHtml((performanceSummary.slowestMeasuredStage || {}).stage || "n/a")}
        · Max heap: ${escapeHtml(performanceSummary.maxEstimatedHeapBytes || 0)} bytes
        · Max framebuffer: ${escapeHtml(performanceSummary.maxFramebufferBytes || 0)} bytes
        · Max display commands: ${escapeHtml(performanceSummary.maxDisplayCommands || 0)}
      </p>
      ${renderList(performanceBottlenecks, (item) => `<li class="advice"><strong>${escapeHtml(item.title || item.code || "Performance bottleneck")}${item.target ? ` <span class="muted">[${escapeHtml(item.target)}]</span>` : ""}</strong>${item.metrics ? ` <span class="muted">Metrics: <code>${escapeHtml(JSON.stringify(item.metrics))}</code></span>` : ""}</li>`)}
      ${renderList(performanceAdvice, (advice) => `<li class="advice"><strong><span class="pill ${escapeHtml(advice.severity || "")}">${escapeHtml(advice.severity || "advice")}</span> ${escapeHtml(advice.title || advice.code || "Performance item")}${advice.target ? ` <span class="muted">[${escapeHtml(advice.target)}]</span>` : ""}</strong><span>${escapeHtml(advice.action || advice.explanation || "")}</span>${advice.metrics ? ` <span class="muted">Metrics: <code>${escapeHtml(JSON.stringify(advice.metrics))}</code></span>` : ""}</li>`)}
    ` : "<p class=\"muted\">No performance summary in the latest report.</p>"}
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
  statusProvider = new JellyFrameStatusProvider(context);
  context.subscriptions.push(
    capabilityDiagnostics,
    statusProvider.changed,
    vscode.window.registerTreeDataProvider("jellyframe.status", statusProvider),
    vscode.commands.registerCommand("jellyframe.validate", (resourceUri) => runPackageCommand(context, "validate", resourceUri)),
    vscode.commands.registerCommand("jellyframe.check", (resourceUri) => runPackageCommand(context, "check", resourceUri)),
    vscode.commands.registerCommand("jellyframe.preview", (resourceUri) => previewPackage(context, resourceUri)),
    vscode.commands.registerCommand("jellyframe.debug", (resourceUri) => debugApp(context, resourceUri)),
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
