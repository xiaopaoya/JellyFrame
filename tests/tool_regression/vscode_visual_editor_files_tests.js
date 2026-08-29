"use strict";

const assert = require("assert");
const fs = require("fs");
const Module = require("module");
const os = require("os");
const path = require("path");

const originalLoad = Module._load;
Module._load = function mockVscode(request, parent, isMain) {
  if (request === "vscode") return { env: { language: "en" } };
  return originalLoad.call(this, request, parent, isMain);
};

const {
  appFiles,
  ensureStylesheet,
  initialModel,
  isBlankStarter,
  isPathInside,
  stylesheetHrefs,
  visualEditorHtml
} = require("../../tools/vscode-jellyframe/visual_editor");
Module._load = originalLoad;

const root = fs.mkdtempSync(path.join(os.tmpdir(), "jellyframe-visual-editor-files-"));
try {
  fs.writeFileSync(path.join(root, "jellyframe.app.json"), JSON.stringify({
    format: "jellyframe.app",
    formatVersion: 0,
    entry: "/index.html"
  }), "utf8");
  const externalOnly = "<!doctype html><html><head><link href=\"https://example.invalid/app.css\" rel=\"preload stylesheet\"></head><body></body></html>";
  fs.writeFileSync(path.join(root, "index.html"), externalOnly, "utf8");

  const files = appFiles(root);
  assert.equal(files.stylesheetHref, "styles/app.css");
  const linked = ensureStylesheet(externalOnly, files.stylesheetHref);
  assert(linked.includes("href=\"styles/app.css\""));
  assert.deepEqual(stylesheetHrefs(linked), ["https://example.invalid/app.css", "styles/app.css"]);

  const localFirst = "<html><head><link href='./theme.css' rel='alternate stylesheet'></head><body></body></html>";
  fs.writeFileSync(path.join(root, "index.html"), localFirst, "utf8");
  assert.equal(appFiles(root).stylesheetHref, "./theme.css");
  assert.equal(ensureStylesheet(localFirst, "./theme.css"), localFirst);

  assert(isPathInside(root, path.join(root, "styles", "app.css")));
  assert(!isPathInside(root, root));
  assert(!isPathInside(root, `${root}-sibling${path.sep}app.css`));

  const blankHtml = "<!doctype html><html><head><link rel=\"stylesheet\" href=\"styles/app.css\"></head><body>\n  <main>Hello world</main>\n  <script src=\"scripts/app.js\"></script>\n</body></html>";
  fs.writeFileSync(path.join(root, "index.html"), blankHtml, "utf8");
  assert(isBlankStarter(blankHtml));
  const blankModel = initialModel(root, appFiles(root));
  assert.equal(blankModel.root.children.length, 1);
  assert.equal(blankModel.root.children[0].text, "Hello world");
  assert.equal(blankModel.root.background, "#ffffff");

  const arbitraryHtml = blankHtml.replace("Hello world", "A different page");
  assert(!isBlankStarter(arbitraryHtml));

  const webviewHtml = visualEditorHtml({ cspSource: "vscode-webview:", asWebviewUri: (uri) => uri }, root, {
    format: "jellyframe.visual-editor",
    formatVersion: 1,
    viewport: { width: 300, height: 300, shape: "round" },
    root: {
      id: "page",
      type: "container",
      layout: "column",
      gap: 0,
      padding: 0,
      width: "100%",
      height: "100%",
      background: "transparent",
      radius: 0,
      align: "stretch",
      justify: "start",
      children: []
    }
  }, {}, { styleUri: "vscode-webview:/visual_editor.css", scriptUri: "vscode-webview:/visual_editor_webview.js" });
  assert(webviewHtml.includes('href="vscode-webview:/visual_editor.css"'), "visual-editor stylesheet is external");
  assert(webviewHtml.includes('src="vscode-webview:/visual_editor_webview.js"'), "visual-editor behavior is external");
  assert(webviewHtml.includes('id="components-tab"'), "components tab is present");
  assert(webviewHtml.includes('id="outline-tab"'), "outline tab is present");
  assert(webviewHtml.includes('id="left-resizer"'), "left panel resizer is present");
  assert(webviewHtml.includes('id="right-resizer"'), "right panel resizer is present");
  assert.doesNotThrow(() => new Function(fs.readFileSync(path.join(__dirname, "../../tools/vscode-jellyframe/visual_editor_webview.js"), "utf8")));

  fs.writeFileSync(path.join(root, "jellyframe.app.json"), "{not json", "utf8");
  assert.throws(() => appFiles(root), /invalid JSON/);
} finally {
  fs.rmSync(root, { recursive: true, force: true });
}

console.log("VS Code visual-editor file tests passed");
