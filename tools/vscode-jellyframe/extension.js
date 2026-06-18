const fs = require("fs");
const path = require("path");
const childProcess = require("child_process");
const vscode = require("vscode");

let outputChannel;

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

function ensureOutputChannel() {
  if (!outputChannel) {
    outputChannel = vscode.window.createOutputChannel("JellyFrame");
  }
  outputChannel.show(true);
  return outputChannel;
}

function runCli(context, args) {
  const python = config().get("pythonPath", "python");
  const cli = cliPath(context);
  const channel = ensureOutputChannel();
  const commandArgs = [cli, ...args];
  channel.appendLine(`+ ${[python, ...commandArgs].join(" ")}`);
  const child = childProcess.spawn(python, commandArgs, {
    cwd: repoRoot(context),
    shell: false
  });
  child.stdout.on("data", (chunk) => channel.append(chunk.toString()));
  child.stderr.on("data", (chunk) => channel.append(chunk.toString()));
  child.on("error", (error) => {
    channel.appendLine(`JellyFrame command failed to start: ${error.message}`);
    vscode.window.showErrorMessage(`JellyFrame command failed to start: ${error.message}`);
  });
  child.on("close", (code) => {
    channel.appendLine(`JellyFrame command exited with code ${code}`);
    if (code !== 0) {
      vscode.window.showErrorMessage(`JellyFrame command failed with code ${code}`);
    }
  });
}

function workspaceFolderPath() {
  const folders = vscode.workspace.workspaceFolders;
  if (!folders || folders.length === 0) {
    return undefined;
  }
  return folders[0].uri.fsPath;
}

function findPackageRootFrom(startPath) {
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
  runCli(context, args);
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
  const output = path.join(buildDir(context), `vscode-${outputBase(root)}.ppm`);
  runCli(context, ["preview", "--root", root, "--target", selectedTarget, "--output", output]);
}

function templateNames(context) {
  const root = path.join(repoRoot(context), "templates", "apps");
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
  context.subscriptions.push(
    vscode.commands.registerCommand("jellyframe.validate", () => runPackageCommand(context, "validate")),
    vscode.commands.registerCommand("jellyframe.check", () => runPackageCommand(context, "check")),
    vscode.commands.registerCommand("jellyframe.preview", () => previewPackage(context)),
    vscode.commands.registerCommand("jellyframe.package", () => runPackageCommand(context, "package")),
    vscode.commands.registerCommand("jellyframe.newFromTemplate", () => newFromTemplate(context))
  );
}

function deactivate() {
  if (outputChannel) {
    outputChannel.dispose();
    outputChannel = undefined;
  }
}

module.exports = {
  activate,
  deactivate
};
