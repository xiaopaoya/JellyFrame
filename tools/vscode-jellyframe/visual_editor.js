"use strict";

const crypto = require("crypto");
const fs = require("fs");
const path = require("path");
const vscode = require("vscode");
const {
  BODY_START,
  BODY_END,
  MAX_NODES,
  boundedModelCheck,
  componentRegistry,
  createDefaultModel,
  defaultNode,
  renderBody,
  recipeRegistry,
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

function localScriptEntries(root, entryPath, html) {
  const entries = [];
  const entryName = `/${path.relative(root, entryPath).replace(/\\/g, "/")}`;
  for (const match of html.matchAll(/<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi)) {
    const attributes = match[1] || "";
    const inlineSource = match[2] || "";
    const src = /\bsrc\s*=\s*(["'])(.*?)\1/i.exec(attributes)?.[2];
    if (!src) {
      if (inlineSource.trim()) entries.push({ source: entryName, text: inlineSource });
      continue;
    }
    if (/^(?:[a-z]+:|\/\/)/i.test(src)) continue;
    const filename = path.resolve(path.dirname(entryPath), src);
    if (!isPathInside(root, filename) || !fs.existsSync(filename) || !fs.statSync(filename).isFile()) continue;
    // The editor only needs a bounded static summary, never a generated bundle.
    if (fs.statSync(filename).size > 256 * 1024) continue;
    entries.push({
      source: `/${path.relative(root, filename).replace(/\\/g, "/")}`,
      text: fs.readFileSync(filename, "utf8")
    });
  }
  return entries;
}

function scriptListenerSummary(root, files, model) {
  const knownIds = new Set();
  walkNodes(model.root, (node) => knownIds.add(node.id));
  const byId = {};
  const add = (id, event, source) => {
    if (!knownIds.has(id)) return;
    const name = String(event || "").toLowerCase();
    if (!/^[a-z][a-z0-9:_-]{0,31}$/.test(name)) return;
    const listeners = byId[id] || (byId[id] = []);
    if (!listeners.some((listener) => listener.event === name && listener.source === source)) {
      listeners.push({ event: name, source });
    }
  };
  const patterns = [
    /(?:document\.)?getElementById\(\s*(["'])([A-Za-z][A-Za-z0-9_-]{0,47})\1\s*\)\s*\.\s*addEventListener\(\s*(["'])([a-z][a-z0-9:_-]{0,31})\3/gi,
    /(?:document\.)?querySelector\(\s*(["'])#([A-Za-z][A-Za-z0-9_-]{0,47})\1\s*\)\s*\.\s*addEventListener\(\s*(["'])([a-z][a-z0-9:_-]{0,31})\3/gi
  ];
  for (const entry of localScriptEntries(root, files.entryPath, files.html)) {
    for (const pattern of patterns) {
      for (const match of entry.text.matchAll(pattern)) add(match[2], match[4], entry.source);
    }
  }
  for (const listeners of Object.values(byId)) {
    listeners.sort((left, right) => left.event.localeCompare(right.event) || left.source.localeCompare(right.source));
  }
  return byId;
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

function backupSources(root, files, html, css, modelText) {
  const directory = path.join(root, ".jellyframe", "visual-editor-backups", timestamp());
  fs.mkdirSync(directory, { recursive: true });
  fs.writeFileSync(path.join(directory, "entry.html"), html, "utf8");
  if (css !== undefined) fs.writeFileSync(path.join(directory, "stylesheet.css"), css, "utf8");
  if (modelText !== undefined) fs.writeFileSync(path.join(directory, "visual-editor.json"), modelText, "utf8");
  return directory;
}

function latestCompleteBackup(root) {
  const backupRoot = path.join(root, ".jellyframe", "visual-editor-backups");
  if (!fs.existsSync(backupRoot)) return undefined;
  const candidates = fs.readdirSync(backupRoot, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => path.join(backupRoot, entry.name))
    .filter((directory) => ["entry.html", "stylesheet.css", "visual-editor.json"].every((name) => fs.existsSync(path.join(directory, name))))
    .filter((directory) => {
      try {
        validateModel(JSON.parse(fs.readFileSync(path.join(directory, "visual-editor.json"), "utf8")));
        return true;
      } catch (_) {
        return false;
      }
    })
    .sort();
  return candidates.at(-1);
}

async function restoreLatestBackup(root, files) {
  const directory = latestCompleteBackup(root);
  if (!directory) throw new Error(isChinese()
    ? "没有可恢复的完整可视化编辑器快照。请先保存一次可视化编辑结果。"
    : "No complete visual-editor snapshot is available. Save a visual-editor change first.");
  const modelPath = path.join(root, MODEL_FILE);
  const modelText = fs.readFileSync(path.join(directory, "visual-editor.json"), "utf8");
  validateModel(JSON.parse(modelText));
  await replaceDocuments([
    [files.entryPath, fs.readFileSync(path.join(directory, "entry.html"), "utf8")],
    [files.stylesheetPath, fs.readFileSync(path.join(directory, "stylesheet.css"), "utf8")],
    [modelPath, modelText]
  ]);
  return directory;
}

async function showSourceDiff(root, files, model) {
  fs.mkdirSync(path.join(root, ".jellyframe"), { recursive: true });
  const directory = fs.mkdtempSync(path.join(root, ".jellyframe", "visual-editor-diffs-"));
  const expectedHtml = updateHtml(ensureStylesheet(workspaceText(files.entryPath), files.stylesheetHref), model);
  const expectedCss = updateCss(workspaceText(files.stylesheetPath));
  const expectedHtmlPath = path.join(directory, "expected-entry.html");
  const expectedCssPath = path.join(directory, "expected-stylesheet.css");
  fs.writeFileSync(expectedHtmlPath, expectedHtml, "utf8");
  fs.writeFileSync(expectedCssPath, expectedCss, "utf8");
  await vscode.commands.executeCommand(
    "vscode.diff",
    vscode.Uri.file(files.entryPath),
    vscode.Uri.file(expectedHtmlPath),
    isChinese() ? "入口 HTML：当前源码与模型生成结果" : "Entry HTML: source vs model output"
  );
  await vscode.commands.executeCommand(
    "vscode.diff",
    vscode.Uri.file(files.stylesheetPath),
    vscode.Uri.file(expectedCssPath),
    isChinese() ? "样式表：当前源码与模型生成结果" : "Stylesheet: source vs model output"
  );
  return directory;
}

function workspaceText(filename, fallback = "") {
  const resolved = path.resolve(filename);
  const document = vscode.workspace.textDocuments.find((candidate) => path.resolve(candidate.uri.fsPath) === resolved);
  if (document) return document.getText();
  return fs.existsSync(filename) ? fs.readFileSync(filename, "utf8") : fallback;
}

function sha256(value) {
  return crypto.createHash("sha256").update(String(value), "utf8").digest("hex");
}

function markedRegion(source, start, end) {
  const startIndex = source.indexOf(start);
  const endIndex = source.indexOf(end);
  if (startIndex < 0 || endIndex < startIndex) return undefined;
  return source.slice(startIndex, endIndex + end.length);
}

function sourceDigests(html, css) {
  const body = markedRegion(html, BODY_START, BODY_END);
  const styles = markedRegion(css, "/* jellyframe-visual-editor:styles:start */", "/* jellyframe-visual-editor:styles:end */");
  return {
    body: body === undefined ? undefined : sha256(body),
    styles: styles === undefined ? undefined : sha256(styles)
  };
}

function hasGeneratedSourceConflict(html, css, model) {
  const expected = model?.sourceDigests;
  if (expected?.body || expected?.styles) {
    const actual = sourceDigests(html, css);
    return (expected.body && actual.body !== expected.body) ||
      (expected.styles && actual.styles !== expected.styles);
  }
  return hasGeneratedBodyConflict(html, model);
}

function documentEdit(api, edit, document, text) {
  const end = document.lineAt(document.lineCount - 1).range.end;
  edit.replace(document.uri, new api.Range(new api.Position(0, 0), end), text);
}

async function replaceDocuments(entries, api = vscode) {
  const workspace = api.workspace;
  const documents = await Promise.all(entries.map(async ([filename]) => workspace.openTextDocument(api.Uri.file(filename))));
  const originals = documents.map((document) => document.getText());
  const edit = new api.WorkspaceEdit();
  for (let index = 0; index < entries.length; index += 1) {
    documentEdit(api, edit, documents[index], entries[index][1]);
  }
  if (!await workspace.applyEdit(edit)) throw new Error("VS Code refused to update the visual-editor source files");

  try {
    for (let index = 0; index < documents.length; index += 1) {
      if (!await documents[index].save()) {
        throw new Error(`VS Code could not save visual-editor source file ${entries[index][0]}`);
      }
    }
  } catch (saveError) {
    const rollbackEdit = new api.WorkspaceEdit();
    for (let index = 0; index < documents.length; index += 1) {
      documentEdit(api, rollbackEdit, documents[index], originals[index]);
    }

    let rollbackError;
    try {
      if (!await workspace.applyEdit(rollbackEdit)) {
        throw new Error("VS Code refused to restore the previous source contents");
      }
      for (let index = 0; index < documents.length; index += 1) {
        if (!await documents[index].save()) {
          throw new Error(`VS Code could not restore ${entries[index][0]}`);
        }
      }
    } catch (error) {
      rollbackError = error;
    }

    if (rollbackError) {
      throw new Error(`Visual-editor source write failed: ${saveError.message}; rollback failed: ${rollbackError.message}`);
    }
    throw new Error(`Visual-editor source write failed and was rolled back: ${saveError.message}`);
  }
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

async function saveModel(root, files, model, state = {}) {
  validateModel(model);
  validatePackageAssets(root, model);
  const currentHtml = workspaceText(files.entryPath);
  const currentCss = workspaceText(files.stylesheetPath);
  const takeoverConfirmed = typeof state === "boolean" ? state : Boolean(state.takeoverConfirmed);
  let confirmed = takeoverConfirmed;
  let conflictConfirmed = typeof state === "object" && Boolean(state.conflictConfirmed);
  const modelPath = path.join(root, MODEL_FILE);
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

  if (hasGeneratedSourceConflict(currentHtml, currentCss, model) && !conflictConfirmed) {
    const overwrite = isChinese() ? "覆盖已修改的生成区域" : "Overwrite generated regions";
    const choice = await vscode.window.showWarningMessage(
      isChinese()
        ? "入口页面或样式表的生成区域已在编辑器外发生变化。为避免静默覆盖，请确认是否用当前模型覆盖这些区域。"
        : "A generated region in the entry page or stylesheet changed outside the editor. Confirm before replacing it with the current model.",
      { modal: true }, overwrite);
    if (choice !== overwrite) return { saved: false, takeoverConfirmed: confirmed, conflictConfirmed: false };
    conflictConfirmed = true;
  }

  const html = updateHtml(ensureStylesheet(currentHtml, files.stylesheetHref), model);
  const css = updateCss(currentCss);
  const savedModel = {
    ...model,
    sourceDigests: sourceDigests(html, css)
  };
  validateModel(savedModel);
  fs.mkdirSync(path.dirname(files.stylesheetPath), { recursive: true });
  if (!fs.existsSync(files.stylesheetPath)) fs.writeFileSync(files.stylesheetPath, "", "utf8");
  fs.mkdirSync(path.dirname(modelPath), { recursive: true });
  if (!fs.existsSync(modelPath)) fs.writeFileSync(modelPath, "", "utf8");
  const previousModelText = fs.readFileSync(modelPath, "utf8");
  let backup;
  try {
    validateModel(JSON.parse(previousModelText));
    backup = backupSources(root, files, currentHtml, currentCss, previousModelText);
  } catch (_) {
    // The first takeover has no previous model to restore; later saves create
    // complete snapshots before replacing the current generation.
  }
  await replaceDocuments([
    [files.entryPath, html],
    [files.stylesheetPath, css],
    [modelPath, `${JSON.stringify(savedModel, null, 2)}\n`]
  ]);
  return { saved: true, model: savedModel, takeoverConfirmed: confirmed, conflictConfirmed, backup };
}

function initialModel(root, files) {
  const stored = readJson(path.join(root, MODEL_FILE));
  if (stored) return validateModel(stored);
  const viewport = files.manifest.viewport || Object.values(files.manifest.targets || {})[0]?.viewport || {};
  if (isBlankStarter(files.html)) return createBlankStarterModel(viewport);
  return createDefaultModel(viewport, files.manifest.name || path.basename(root));
}

function isVisualEditorPackage(root) {
  const modelPath = path.join(root, MODEL_FILE);
  if (!fs.existsSync(modelPath)) return false;
  try {
    return Boolean(validateModel(readJson(modelPath)));
  } catch (_) {
    return false;
  }
}

function isVisualEditorEligible(root) {
  if (isVisualEditorPackage(root)) return true;
  try {
    return isBlankStarter(appFiles(root).html);
  } catch (_) {
    return false;
  }
}

function hasGeneratedBodyConflict(html, model) {
  const start = html.indexOf(BODY_START);
  const end = html.indexOf(BODY_END);
  if (start < 0 || end < start) return false;
  return html.slice(start, end + BODY_END.length) !== renderBody(model);
}

function isBlankStarter(html) {
  const body = /<body(?:\s[^>]*)?>([\s\S]*?)<\/body\s*>/i.exec(html)?.[1] || "";
  const withoutScripts = body.replace(/<script\b[^>]*>[\s\S]*?<\/script\s*>/gi, "").trim();
  return /^<main>\s*Hello world\s*<\/main>$/i.test(withoutScripts);
}

function createBlankStarterModel(viewport) {
  const model = createDefaultModel(viewport, "Blank App");
  model.root.padding = 0;
  model.root.gap = 0;
  model.root.justify = "start";
  model.root.background = "#000000";
  model.root.children = [{
    ...defaultNode("text", "main"),
    text: "Hello world",
    fontSize: 16,
    color: "#f4f7fb",
    align: "left",
    width: "100%"
  }];
  return model;
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
  const payload = JSON.stringify({
    model,
    assets,
    chinese: isChinese(),
    appName: path.basename(root),
    maxNodes: MAX_NODES,
    registry: componentRegistry(),
    recipes: recipeRegistry(),
    interactions: resources.interactions || {},
    sourceConflict: Boolean(resources.sourceConflict)
  }).replace(/</g, "\\u003c");
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
    <div id="document-title"><span id="save-indicator"></span><strong id="app-name"></strong><span id="document-state"></span><span id="runtime-state"></span></div>
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
      <div id="source-notice" hidden role="alert"></div>
      <div id="canvas-wrap"><div id="device-column"><div id="device-caption"></div><div id="canvas-shell"><div id="canvas"></div></div></div><div id="canvas-floating-toolbar" role="toolbar" aria-label="Canvas tools">
        <button id="canvas-tool-undo" class="icon-button quiet" type="button" aria-label="Undo" title="Undo">&#8630;</button>
        <button id="canvas-tool-redo" class="icon-button quiet" type="button" aria-label="Redo" title="Redo">&#8631;</button>
        <span class="canvas-tool-divider" aria-hidden="true"></span>
        <button id="canvas-tool-fit" class="icon-button quiet" type="button" aria-label="Fit canvas" title="Fit canvas"><span class="toolbar-glyph toolbar-glyph-fit" aria-hidden="true"></span></button>
        <button id="canvas-tool-zoom-out" class="icon-button quiet" type="button" aria-label="Zoom out" title="Zoom out">&#8722;</button>
        <button id="canvas-tool-zoom-in" class="icon-button quiet" type="button" aria-label="Zoom in" title="Zoom in">+</button>
        <span class="canvas-tool-divider" aria-hidden="true"></span>
        <button id="canvas-tool-outline" class="icon-button quiet" type="button" aria-label="Show structure" title="Show structure">&#9776;</button>
        <button id="canvas-tool-save" class="icon-button quiet" type="button" aria-label="Save" title="Save"><span class="toolbar-glyph toolbar-glyph-save" aria-hidden="true"></span></button>
      </div></div>
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
  let conflictConfirmed = false;
  panel.webview.html = visualEditorHtml(panel.webview, root, model, assetMap(panel.webview, root, model), {
    styleUri,
    scriptUri,
    interactions: scriptListenerSummary(root, files, model),
    sourceConflict: hasGeneratedSourceConflict(files.html, workspaceText(files.stylesheetPath), model)
  });

  const messageDisposable = panel.webview.onDidReceiveMessage(async (message) => {
    try {
      if (message?.type === "save" || message?.type === "save-debug") {
        const outcome = await saveModel(root, files, message.model, { takeoverConfirmed, conflictConfirmed });
        takeoverConfirmed = outcome.takeoverConfirmed;
        conflictConfirmed = outcome.conflictConfirmed;
        if (outcome.saved) {
          files = appFiles(root);
          model = outcome.model;
          conflictConfirmed = false;
          panel.webview.postMessage({
            type: "saved",
            debug: message.type === "save-debug",
            interactions: scriptListenerSummary(root, files, model)
          });
        } else panel.webview.postMessage({ type: "save-cancelled" });
      } else if (message?.type === "model-check") {
        const revision = Number.isInteger(message.revision) ? message.revision : 0;
        try {
          const result = boundedModelCheck(message.model);
          panel.webview.postMessage({
            type: "model-check-result",
            revision,
            ok: true,
            nodeCount: result.nodeCount,
            generatedBytes: result.generatedBytes
          });
        } catch (error) {
          panel.webview.postMessage({
            type: "model-check-result",
            revision,
            ok: false,
            message: error.message
          });
        }
      } else if (message?.type === "debug") {
        await vscode.commands.executeCommand("jellyframe.debug", vscode.Uri.file(root), {
          requestedViewport: model.viewport,
          visualEditorPanel: panel
        });
      } else if (message?.type === "copy-event-skeleton" && typeof message.code === "string") {
        if (message.code.length > 4096) throw new Error("Visual-editor event skeleton is too large to copy");
        await vscode.env.clipboard.writeText(message.code);
        vscode.window.showInformationMessage(isChinese() ? "事件骨架已复制到剪贴板。" : "Event skeleton copied to the clipboard.");
      } else if (message?.type === "show-source-diff") {
        await showSourceDiff(root, files, model);
      } else if (message?.type === "restore-backup") {
        const accept = isChinese() ? "恢复最近快照" : "Restore latest snapshot";
        const choice = await vscode.window.showWarningMessage(
          isChinese()
            ? "这会用最近一次完整快照替换入口 HTML、样式表和可视化模型，当前未保存内容会丢失。"
            : "This replaces the entry HTML, stylesheet, and visual model with the latest complete snapshot. Unsaved changes will be lost.",
          { modal: true }, accept
        );
        if (choice !== accept) return;
        await restoreLatestBackup(root, files);
        files = appFiles(root);
        model = initialModel(root, files);
        takeoverConfirmed = files.html.includes(BODY_START);
        conflictConfirmed = false;
        panel.webview.html = visualEditorHtml(panel.webview, root, model, assetMap(panel.webview, root, model), {
          styleUri,
          scriptUri,
          interactions: scriptListenerSummary(root, files, model),
          sourceConflict: hasGeneratedSourceConflict(files.html, workspaceText(files.stylesheetPath), model)
        });
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
  const sourceWatcher = vscode.workspace.onDidSaveTextDocument((document) => {
    try {
      const filename = path.resolve(document.uri.fsPath);
      if (filename === path.resolve(files.entryPath)) files = appFiles(root);
      const scripts = new Set(localScriptEntries(root, files.entryPath, workspaceText(files.entryPath))
        .map((entry) => path.resolve(root, entry.source.replace(/^[/\\]+/, ""))));
      if (scripts.has(filename)) {
        panel.webview.postMessage({ type: "interactions", interactions: scriptListenerSummary(root, files, model) });
        return;
      }
      if (filename !== path.resolve(files.entryPath) && filename !== path.resolve(files.stylesheetPath)) return;
      const conflict = hasGeneratedSourceConflict(
        workspaceText(files.entryPath),
        workspaceText(files.stylesheetPath),
        model
      );
      panel.webview.postMessage({
        type: "source-conflict",
        conflict,
        interactions: filename === path.resolve(files.entryPath) ? scriptListenerSummary(root, files, model) : undefined
      });
    } catch (error) {
      panel.webview.postMessage({ type: "error", message: error.message });
    }
  });
  panel.onDidDispose(() => messageDisposable.dispose());
  panel.onDidDispose(() => sourceWatcher.dispose());
}

module.exports = { appFiles, ensureStylesheet, hasGeneratedBodyConflict, hasGeneratedSourceConflict, initialModel, isBlankStarter, isPathInside, isVisualEditorEligible, isVisualEditorPackage, latestCompleteBackup, localScriptEntries, openVisualEditor, replaceDocuments, restoreLatestBackup, saveModel, scriptListenerSummary, showSourceDiff, sourceDigests, stylesheetHrefs, visualEditorHtml };
