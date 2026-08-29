"use strict";

const fs = require("fs");
const path = require("path");
const vscode = require("vscode");
const {
  BODY_START,
  MAX_NODES,
  createDefaultModel,
  updateCss,
  updateHtml,
  validateModel,
  walkNodes
} = require("./visual_editor_model");

const MODEL_FILE = path.join(".jellyframe", "visual-editor.json");

function isChinese() {
  return /^zh(?:-|$)/i.test(vscode.env.language || "");
}

function readJson(filename) {
  try {
    return JSON.parse(fs.readFileSync(filename, "utf8"));
  } catch (error) {
    if (fs.existsSync(filename)) throw new Error(`${path.basename(filename)} contains invalid JSON: ${error.message}`);
    return undefined;
  }
}

function isPathInside(root, candidate) {
  const relative = path.relative(path.resolve(root), path.resolve(candidate));
  return relative !== "" && relative !== ".." && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative);
}

function stylesheetHrefs(html) {
  const hrefs = [];
  for (const match of html.matchAll(/<link\b[^>]*>/gi)) {
    const tag = match[0];
    const rel = /\brel\s*=\s*(["'])(.*?)\1/i.exec(tag)?.[2] || "";
    if (!rel.split(/\s+/).some((value) => value.toLowerCase() === "stylesheet")) continue;
    const href = /\bhref\s*=\s*(["'])(.*?)\1/i.exec(tag)?.[2];
    if (href) hrefs.push(href);
  }
  return hrefs;
}

function appFiles(root) {
  const manifestPath = path.join(root, "jellyframe.app.json");
  const legacyPath = path.join(root, "app.json");
  const actualManifest = fs.existsSync(manifestPath) ? manifestPath : legacyPath;
  if (!fs.existsSync(actualManifest)) {
    throw new Error(isChinese() ? "当前目录不包含 JellyFrame App manifest。" : "The selected directory does not contain a JellyFrame App manifest.");
  }
  const manifest = readJson(actualManifest);
  const entryValue = String(manifest.entry || "/index.html").replace(/^[/\\]+/, "");
  const entryPath = path.resolve(root, entryValue);
  if (!isPathInside(root, entryPath) || !fs.existsSync(entryPath)) {
    throw new Error(isChinese() ? "App manifest 的入口文件无效。" : "The App manifest entry is invalid.");
  }
  const html = fs.readFileSync(entryPath, "utf8");
  const stylesheetHref = stylesheetHrefs(html).find((href) => {
    if (/^(?:[a-z]+:|\/\/)/i.test(href)) return false;
    return isPathInside(root, path.resolve(path.dirname(entryPath), href));
  }) || "styles/app.css";
  const stylesheetPath = path.resolve(path.dirname(entryPath), stylesheetHref);
  if (!isPathInside(root, stylesheetPath)) {
    throw new Error(isChinese() ? "App 样式表必须位于包目录内。" : "The App stylesheet must be inside the package root.");
  }
  return { manifest, manifestPath: actualManifest, entryPath, stylesheetPath, stylesheetHref, html };
}

function ensureStylesheet(html, href) {
  const normalized = href.replace(/\\/g, "/").replace(/^\.\//, "");
  if (stylesheetHrefs(html).some((value) => value.replace(/\\/g, "/").replace(/^\.\//, "") === normalized)) return html;
  const link = `  <link rel="stylesheet" href="${href.replace(/\\/g, "/")}">\n`;
  return /<\/head\s*>/i.test(html) ? html.replace(/<\/head\s*>/i, `${link}</head>`) : html;
}

function timestamp() {
  return new Date().toISOString().replace(/[:.]/g, "-");
}

function backupSources(root, files, html, css) {
  const directory = path.join(root, ".jellyframe", "visual-editor-backups", timestamp());
  fs.mkdirSync(directory, { recursive: true });
  fs.writeFileSync(path.join(directory, path.basename(files.entryPath)), html, "utf8");
  if (css !== undefined) fs.writeFileSync(path.join(directory, path.basename(files.stylesheetPath)), css, "utf8");
  return directory;
}

function workspaceText(filename, fallback = "") {
  const resolved = path.resolve(filename);
  const document = vscode.workspace.textDocuments.find((candidate) => path.resolve(candidate.uri.fsPath) === resolved);
  if (document) return document.getText();
  return fs.existsSync(filename) ? fs.readFileSync(filename, "utf8") : fallback;
}

async function replaceDocuments(entries) {
  const documents = await Promise.all(entries.map(async ([filename]) => vscode.workspace.openTextDocument(vscode.Uri.file(filename))));
  const edit = new vscode.WorkspaceEdit();
  for (let index = 0; index < entries.length; index += 1) {
    const document = documents[index];
    const end = document.lineAt(document.lineCount - 1).range.end;
    edit.replace(document.uri, new vscode.Range(new vscode.Position(0, 0), end), entries[index][1]);
  }
  if (!await vscode.workspace.applyEdit(edit)) throw new Error("VS Code refused to update the visual-editor source files");
  const saved = await Promise.all(documents.map((document) => document.save()));
  if (saved.some((outcome) => !outcome)) throw new Error("VS Code could not save every visual-editor source file");
}

function validatePackageAssets(root, model) {
  walkNodes(model.root, (node) => {
    if (node.type !== "image") return;
    const filename = path.resolve(root, String(node.src).replace(/^[/\\]+/, ""));
    if (!isPathInside(root, filename) || !fs.existsSync(filename) || !fs.statSync(filename).isFile()) {
      throw new Error(isChinese()
        ? `图片 ${node.id} 必须引用当前 App 包内已存在的文件。`
        : `Image ${node.id} must reference an existing file inside the current App package.`);
    }
  });
}

async function saveModel(root, files, model, takeoverConfirmed) {
  validateModel(model);
  validatePackageAssets(root, model);
  const currentHtml = workspaceText(files.entryPath);
  const currentCss = workspaceText(files.stylesheetPath);
  let confirmed = takeoverConfirmed;
  if (!currentHtml.includes(BODY_START) && !confirmed) {
    const accept = isChinese() ? "初始化可视化编辑" : "Initialize visual editing";
    const choice = await vscode.window.showWarningMessage(
      isChinese()
        ? "首次保存将由可视化编辑器接管入口页面的 body，并在 .jellyframe/visual-editor-backups 中保存原页面与样式。脚本文件不会被修改。"
        : "The first save lets the visual editor own the entry-page body and stores the original page and stylesheet under .jellyframe/visual-editor-backups. Script files are not modified.",
      { modal: true }, accept);
    if (choice !== accept) return { saved: false, takeoverConfirmed: false };
    const backup = backupSources(root, files, currentHtml, currentCss);
    vscode.window.showInformationMessage(isChinese() ? `原页面已备份：${backup}` : `Original page backed up: ${backup}`);
    confirmed = true;
  }

  const html = updateHtml(ensureStylesheet(currentHtml, files.stylesheetHref), model);
  const css = updateCss(currentCss);
  fs.mkdirSync(path.dirname(files.stylesheetPath), { recursive: true });
  if (!fs.existsSync(files.stylesheetPath)) fs.writeFileSync(files.stylesheetPath, "", "utf8");
  const modelPath = path.join(root, MODEL_FILE);
  fs.mkdirSync(path.dirname(modelPath), { recursive: true });
  if (!fs.existsSync(modelPath)) fs.writeFileSync(modelPath, "", "utf8");
  await replaceDocuments([
    [files.entryPath, html],
    [files.stylesheetPath, css],
    [modelPath, `${JSON.stringify(model, null, 2)}\n`]
  ]);
  return { saved: true, takeoverConfirmed: confirmed };
}

function initialModel(root, files) {
  const stored = readJson(path.join(root, MODEL_FILE));
  if (stored) return validateModel(stored);
  const viewport = files.manifest.viewport || Object.values(files.manifest.targets || {})[0]?.viewport || {};
  return createDefaultModel(viewport, files.manifest.name || path.basename(root));
}

function assetMap(webview, root, model) {
  const assets = {};
  walkNodes(model.root, (node) => {
    if (node.type !== "image" || typeof node.src !== "string") return;
    const filename = path.resolve(root, node.src.replace(/^[/\\]+/, ""));
    if (isPathInside(root, filename) && fs.existsSync(filename)) {
      assets[node.src] = webview.asWebviewUri(vscode.Uri.file(filename)).toString();
    }
  });
  return assets;
}

function nonce() {
  return `${Date.now()}${Math.random().toString(16).slice(2)}`;
}

function visualEditorHtml(webview, root, model, assets, resources = {}) {
  const token = nonce();
  const payload = JSON.stringify({ model, assets, chinese: isChinese(), appName: path.basename(root), maxNodes: MAX_NODES }).replace(/</g, "\\u003c");
  const styleUri = String(resources.styleUri || "visual_editor.css");
  const scriptUri = String(resources.scriptUri || "visual_editor_webview.js");
  return `<!doctype html>
<html lang="${isChinese() ? "zh-CN" : "en"}">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src ${webview.cspSource} data:; style-src ${webview.cspSource}; script-src 'nonce-${token}' ${webview.cspSource};">
  <link rel="stylesheet" href="${styleUri}">
</head>
<body>
  <header id="topbar">
    <div id="document-title"><span id="save-indicator"></span><strong id="app-name"></strong><span id="document-state"></span></div>
    <div class="toolbar-group" id="panel-controls">
      <button id="toggle-left" class="icon-button quiet" type="button" aria-label="Toggle left panel" title="Toggle left panel">&#9776;</button>
      <button id="toggle-right" class="icon-button quiet" type="button" aria-label="Toggle inspector" title="Toggle inspector">&#8942;</button>
    </div>
    <div class="toolbar-group history-controls">
      <button id="undo" class="icon-button quiet" type="button" aria-label="Undo" title="Undo">&#8630;</button>
      <button id="redo" class="icon-button quiet" type="button" aria-label="Redo" title="Redo">&#8631;</button>
    </div>
    <div class="toolbar-group primary-actions">
      <button id="actual" class="secondary" type="button"></button>
      <button id="save" class="primary" type="button"></button>
    </div>
  </header>
  <div id="workspace">
    <aside id="left-panel" class="side-panel">
      <div class="panel-tabs" role="tablist">
        <button class="panel-tab active" id="components-tab" data-panel="components" type="button" role="tab" aria-selected="true"></button>
        <button class="panel-tab" id="outline-tab" data-panel="outline" type="button" role="tab" aria-selected="false"></button>
      </div>
      <section id="components-panel" class="panel-view" role="tabpanel"><div id="palette-list"></div></section>
      <section id="outline-panel" class="panel-view" role="tabpanel" hidden><div id="outline-tree" role="tree"></div></section>
    </aside>
    <div id="left-resizer" class="panel-resizer" role="separator" aria-label="Resize left panel"></div>
    <main id="stage">
      <div id="stage-toolbar">
        <div class="viewport-control"><label id="viewport-label" for="viewport-preset"></label><select id="viewport-preset">
          <option value="model"></option><option value="round-300">300 x 300</option><option value="rect-172x320">172 x 320</option><option value="rect-320x240">320 x 240</option>
        </select></div>
        <div class="zoom-controls"><button id="zoom-out" class="icon-button quiet" type="button" aria-label="Zoom out" title="Zoom out">&#8722;</button><button id="zoom-fit" class="quiet" type="button"></button><button id="zoom-in" class="icon-button quiet" type="button" aria-label="Zoom in" title="Zoom in">+</button><span id="zoom-label">100%</span></div>
      </div>
      <div id="canvas-wrap"><div id="device-column"><div id="device-caption"></div><div id="canvas-shell"><div id="canvas"></div></div></div></div>
      <div id="breadcrumbs" aria-label="Selection path"></div>
    </main>
    <div id="right-resizer" class="panel-resizer" role="separator" aria-label="Resize inspector"></div>
    <aside id="inspector" class="side-panel"><div id="inspector-heading"><div><span id="selected-type"></span><strong id="selected-name"></strong></div><div id="node-actions"></div></div><div id="inspector-body"></div></aside>
  </div>
  <footer id="statusbar"><span id="status"></span><span id="node-count"></span></footer>
  <script id="visual-editor-initial" type="application/json" nonce="${token}">${payload}</script>
  <script src="${scriptUri}" nonce="${token}"></script>
</body>
</html>`;
}

async function openVisualEditor(context, root) {
  let files;
  let model;
  try {
    files = appFiles(root);
    model = initialModel(root, files);
  } catch (error) {
    vscode.window.showErrorMessage(`${isChinese() ? "无法打开可视化编辑器" : "Unable to open visual editor"}: ${error.message}`);
    return;
  }

  const panel = vscode.window.createWebviewPanel(
    "jellyframeVisualEditor",
    `${isChinese() ? "JellyFrame 可视化编辑" : "JellyFrame Visual Editor"}: ${path.basename(root)}`,
    vscode.ViewColumn.Beside,
    { enableScripts: true, retainContextWhenHidden: true, localResourceRoots: [vscode.Uri.file(root), context.extensionUri] }
  );
  const styleUri = panel.webview.asWebviewUri(vscode.Uri.joinPath(context.extensionUri, "visual_editor.css"));
  const scriptUri = panel.webview.asWebviewUri(vscode.Uri.joinPath(context.extensionUri, "visual_editor_webview.js"));
  let takeoverConfirmed = files.html.includes(BODY_START);
  panel.webview.html = visualEditorHtml(panel.webview, root, model, assetMap(panel.webview, root, model), { styleUri, scriptUri });

  const messageDisposable = panel.webview.onDidReceiveMessage(async (message) => {
    try {
      if (message?.type === "save" || message?.type === "save-debug") {
        const outcome = await saveModel(root, files, message.model, takeoverConfirmed);
        takeoverConfirmed = outcome.takeoverConfirmed;
        if (outcome.saved) {
          files = appFiles(root);
          panel.webview.postMessage({ type: "saved", debug: message.type === "save-debug" });
        } else panel.webview.postMessage({ type: "save-cancelled" });
      } else if (message?.type === "debug") {
        await vscode.commands.executeCommand("jellyframe.debug", vscode.Uri.file(root));
      } else if (message?.type === "choose-image" && typeof message.nodeId === "string") {
        const picked = await vscode.window.showOpenDialog({
          defaultUri: vscode.Uri.file(path.join(root, "assets")),
          canSelectFiles: true,
          canSelectFolders: false,
          canSelectMany: false,
          filters: { Images: ["bmp", "png", "jpg", "jpeg", "webp"] },
          openLabel: isChinese() ? "选择包内图片" : "Select package image"
        });
        const filename = picked?.[0]?.fsPath;
        if (!filename) return;
        if (!isPathInside(root, filename)) {
          vscode.window.showErrorMessage(isChinese() ? "图片必须位于当前 App 包内。" : "The image must be inside the current App package.");
          return;
        }
        const relative = `/${path.relative(root, filename).replace(/\\/g, "/")}`;
        panel.webview.postMessage({ type: "asset", nodeId: message.nodeId, path: relative, uri: panel.webview.asWebviewUri(vscode.Uri.file(filename)).toString() });
      }
    } catch (error) {
      panel.webview.postMessage({ type: "error", message: error.message });
      vscode.window.showErrorMessage(`${isChinese() ? "可视化编辑失败" : "Visual editor failed"}: ${error.message}`);
    }
  });
  panel.onDidDispose(() => messageDisposable.dispose());
}

module.exports = { appFiles, ensureStylesheet, initialModel, isPathInside, openVisualEditor, saveModel, stylesheetHrefs, visualEditorHtml };
