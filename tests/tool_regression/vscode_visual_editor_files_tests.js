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

const { updateCss } = require("../../tools/vscode-jellyframe/visual_editor_model");

const {
  appFiles,
  ensureStylesheet,
  hasGeneratedBodyConflict,
  hasGeneratedSourceConflict,
  initialModel,
  isBlankStarter,
  isVisualEditorEligible,
  isVisualEditorPackage,
  isPathInside,
  latestCompleteBackup,
  scriptListenerSummary,
  sourceDigests,
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
  assert.equal(blankModel.root.background, "#000000");
  assert(!isVisualEditorPackage(root));
  assert(isVisualEditorEligible(root), "standard blank starter is eligible for visual editing");
  fs.mkdirSync(path.join(root, "scripts"), { recursive: true });
  fs.writeFileSync(path.join(root, "scripts", "app.js"), [
    'document.getElementById("main").addEventListener("click", () => {});',
    'document.querySelector("#main").addEventListener("change", () => {});',
    'document.getElementById("unknown").addEventListener("click", () => {});'
  ].join("\n"), "utf8");
  assert.deepEqual(scriptListenerSummary(root, appFiles(root), blankModel), {
    main: [
      { event: "change", source: "/scripts/app.js" },
      { event: "click", source: "/scripts/app.js" }
    ]
  });
  fs.mkdirSync(path.join(root, ".jellyframe"), { recursive: true });
  fs.writeFileSync(path.join(root, ".jellyframe", "visual-editor.json"), JSON.stringify(blankModel), "utf8");
  assert(isVisualEditorPackage(root));

  const arbitraryHtml = blankHtml.replace("Hello world", "A different page");
  assert(!isBlankStarter(arbitraryHtml));
const generatedHtml = blankHtml.replace(/<body>[\s\S]*?<\/body>/i, `<body>\n${require("../../tools/vscode-jellyframe/visual_editor_model").renderBody(blankModel)}\n</body>`);
const generatedCss = updateCss("");
const savedModel = { ...blankModel, sourceDigests: sourceDigests(generatedHtml, generatedCss) };
assert(!hasGeneratedBodyConflict(generatedHtml, blankModel));
assert(hasGeneratedBodyConflict(generatedHtml.replace("Hello world", "Changed outside"), blankModel));
assert(!hasGeneratedSourceConflict(generatedHtml, generatedCss, savedModel));
assert(hasGeneratedSourceConflict(generatedHtml.replace("Hello world", "Changed outside"), generatedCss, savedModel));
assert(hasGeneratedSourceConflict(generatedHtml, generatedCss.replace("overflow: hidden", "overflow: scroll"), savedModel));

  const backupDirectory = path.join(root, ".jellyframe", "visual-editor-backups", "2026-08-30T00-00-00-000Z");
  fs.mkdirSync(backupDirectory, { recursive: true });
  fs.writeFileSync(path.join(backupDirectory, "entry.html"), generatedHtml, "utf8");
  fs.writeFileSync(path.join(backupDirectory, "stylesheet.css"), generatedCss, "utf8");
  fs.writeFileSync(path.join(backupDirectory, "visual-editor.json"), JSON.stringify(savedModel), "utf8");
  assert.equal(latestCompleteBackup(root), backupDirectory);

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
  assert(webviewHtml.includes('id="source-notice"'), "source conflict notice is present");
  assert(webviewHtml.includes('id="left-resizer"'), "left panel resizer is present");
assert(webviewHtml.includes('id="right-resizer"'), "right panel resizer is present");
assert(webviewHtml.includes('id="canvas-floating-toolbar"'), "canvas floating toolbar is present");
assert(webviewHtml.includes('id="canvas-tool-save"'), "canvas floating toolbar exposes save");
assert(webviewHtml.includes('id="canvas-tool-save"') && !webviewHtml.includes('&#128190;'), "canvas save icon must stay monochrome");
assert(webviewHtml.includes("toolbar-glyph-fit") && webviewHtml.includes("toolbar-glyph-save"), "fit and save use explicit monochrome glyphs");
assert(webviewHtml.includes('"registry"'), "component registry is passed to the webview");
assert(webviewHtml.includes('"recipes"'), "recipe registry is passed to the webview");
  assert(webviewHtml.includes('"renderKey"'), "renderer contract is passed to the webview");
  const visualEditorCss = fs.readFileSync(path.join(__dirname, "../../tools/vscode-jellyframe/visual_editor.css"), "utf8");
  const visualEditorWebview = fs.readFileSync(path.join(__dirname, "../../tools/vscode-jellyframe/visual_editor_webview.js"), "utf8");
  assert(visualEditorWebview.includes("bindCanvasPan"), "canvas must support panning");
  assert(visualEditorWebview.includes("showDropPreview"), "dragging a new component must show a live preview");
  assert(visualEditorWebview.includes("source.style.display = \"none\""), "moving nodes must remove their original layout slot");
  assert(visualEditorWebview.includes("source.cloneNode(true)"), "moving nodes must preview the original control shape");
assert(visualEditorWebview.includes("designer-drop-preview-move"), "moving nodes must use a dedicated preview class");
assert(visualEditorWebview.includes("stringListField"), "list-valued fields use the bounded inspector control");
assert(visualEditorWebview.includes("canvas-tool-outline"), "canvas toolbar can open the structure panel");
assert(!visualEditorWebview.includes("element.style.minHeight = \"8px\""), "structural nodes must preserve their declared runtime height");
assert(!visualEditorWebview.includes("element.style.minHeight = \"18px\""), "spacers must preserve their declared runtime height");
assert(visualEditorWebview.includes("booleanField"), "switch uses a boolean inspector control");
assert(visualEditorWebview.includes("interactionSection"), "the inspector exposes an interaction summary");
assert(visualEditorWebview.includes("copy-event-skeleton"), "the editor can copy a non-destructive event skeleton");
assert(visualEditorWebview.includes("bindPointerDrag(button, { kind: \"recipe\", type: recipe.type }"), "recipes must support pointer drag insertion");
assert(visualEditorWebview.includes('payload.kind === "recipe"'), "recipe drops must expand their ordinary node tree");
assert(visualEditorWebview.includes("button.style.boxSizing = \"border-box\""), "navigation buttons must match generated box sizing");
assert(visualEditorWebview.includes("function styleLength(value)"), "design canvas must serialize numeric dimensions as CSS lengths");
assert(visualEditorWebview.includes("button.style.fontSize"), "navigation buttons must receive an explicit runtime-equivalent font size");
assert(!visualEditorWebview.includes('button.style.font = "inherit"'), "canvas must not depend on browser-only font inheritance");
  assert(visualEditorWebview.includes("targetId = undefined"), "leaving a drop target must clear stale focus");
  assert(visualEditorWebview.includes("event.stopPropagation();"), "nested pointer drags must not start ancestor drags");
  assert(visualEditorWebview.includes("if (node.id !== model.root.id) {\n      bindPointerDrag"), "the page root must not be draggable");
  assert(visualEditorWebview.includes("--pan-x"), "canvas grid must follow the viewport pan");
  assert(visualEditorWebview.includes("panX,\n      panY"), "canvas pan position must persist");
  assert(visualEditorWebview.includes("element.draggable = false"), "pointer drag must be the single drag path");
  assert(visualEditorCss.includes("pointer-events: none"), "drag ghost must not block hit testing");
  assert(visualEditorCss.includes("designer-drop-preview"), "live drop preview must have a visible style");
  assert(visualEditorCss.includes("opacity: 0.45"), "moving nodes must use a translucent preview");
  assert(visualEditorCss.includes("#source-notice[hidden]"), "hidden source notice keeps its grid row");
  assert.doesNotThrow(() => new Function(visualEditorWebview));

  fs.writeFileSync(path.join(root, "jellyframe.app.json"), "{not json", "utf8");
  assert.throws(() => appFiles(root), /invalid JSON/);
} finally {
  fs.rmSync(root, { recursive: true, force: true });
}

console.log("VS Code visual-editor file tests passed");
