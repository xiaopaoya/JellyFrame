"use strict";

// Run with Playwright on NODE_PATH; browser channel defaults to installed Edge.
const assert = require("assert/strict");
const fs = require("fs");
const path = require("path");
const Module = require("module");
const { chromium } = require("playwright");
const originalLoad = Module._load;
Module._load = function(request, parent, isMain) {
  if (request === "vscode") return { env: { language: "en" } };
  return originalLoad.call(this, request, parent, isMain);
};
const { visualEditorHtml } = require("../../tools/vscode-jellyframe/visual_editor");
Module._load = originalLoad;
const { createDefaultModel, defaultNode, componentRegistry, recipeRegistry } = require("../../tools/vscode-jellyframe/visual_editor_model");
const editor = path.resolve(__dirname, "../../tools/vscode-jellyframe");
const output = path.resolve(process.env.JELLYFRAME_UI_TEST_OUTPUT || "build/test_outputs/visual_editor");
const errors = [];

function fixture() {
  const model = createDefaultModel({ width: 320, height: 600, shape: "rect" });
  model.root.padding = 8;
  model.root.gap = 8;
  const source = defaultNode("container", "source");
  Object.assign(source, { height: "100px", padding: 4 });
  source.children = [defaultNode("button", "moving")];
  const target = defaultNode("container", "target");
  Object.assign(target, { width: "80%", height: "240px", padding: 8, gap: 9 });
  model.root.children = [source, target];
  return model;
}

async function mount(page, model, state = {}) {
  const html = visualEditorHtml({ cspSource: "", asWebviewUri: (uri) => uri }, "interaction-test", model, {})
    .replace(/<meta http-equiv="Content-Security-Policy"[^>]*>/g, "")
    .replace(/<link[^>]*>/g, "")
    .replace(/<script[^>]*src=[^>]*><\/script>/g, "");
  await page.goto("about:blank");
  await page.setContent(html);
  await page.evaluate((initialState) => {
    window.testState = initialState;
    window.messages = [];
    window.acquireVsCodeApi = () => ({
      getState: () => window.testState,
      setState: (value) => { window.testState = value; },
      postMessage: (value) => window.messages.push(value)
    });
  }, { activePanel: "outline", zoom: 1, leftCollapsed: false, rightCollapsed: false, ...state });
  await page.addStyleTag({ path: path.join(editor, "visual_editor.css") });
  await page.addScriptTag({ path: path.join(editor, "visual_editor_webview.js") });
}

const row = (page, id) => page.locator(`#outline-tree .outline-row[data-node-id="${id}"]`);
const canvasNode = (page, id) => page.locator(`#canvas .designer-node[data-node-id="${id}"]`);
async function activeId(page) { return page.evaluate(() => document.activeElement?.dataset.nodeId); }
async function selectedId(page) { return page.locator(".outline-row.selected").getAttribute("data-node-id"); }
async function geometry(page) {
  return page.locator("#canvas .designer-node").evaluateAll((nodes) => nodes.map((node) => {
    const { x, y, width, height } = node.getBoundingClientRect();
    return { id: node.dataset.nodeId, x, y, width, height };
  }));
}
function sameGeometry(before, after) {
  assert.deepEqual(before.map((n) => n.id), after.map((n) => n.id));
  before.forEach((node, i) => {
    for (const key of ["x", "y", "width", "height"]) {
      assert(Math.abs(node[key] - after[i][key]) < 0.1, `${node.id}.${key}: preview=${node[key]}, drop=${after[i][key]}`);
    }
  });
}
async function dragTo(page, source, target, fraction = { x: 0.5, y: 0.5 }) {
  await source.scrollIntoViewIfNeeded();
  const a = await source.boundingBox();
  const b = await target.boundingBox();
  await page.mouse.move(a.x + a.width / 2, a.y + a.height / 2);
  await page.mouse.down();
  await page.mouse.move(b.x + b.width * fraction.x, b.y + b.height * fraction.y, { steps: 8 });
  await page.locator("#canvas .designer-drop-preview").waitFor();
}

async function main() {
  fs.mkdirSync(output, { recursive: true });
  const browser = await chromium.launch({ channel: process.env.JELLYFRAME_TEST_BROWSER || "msedge", headless: true });
  try {
    const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
    page.on("pageerror", (error) => errors.push(error.message));
    await mount(page, fixture());
    await page.locator("#outline-tab").focus();
    await page.keyboard.press("Tab");
    assert.equal(await activeId(page), "page");
    await page.keyboard.press("ArrowDown");
    assert.equal(await activeId(page), "source");
    await page.keyboard.press("ArrowLeft");
    assert.equal(await row(page, "source").getAttribute("aria-expanded"), "false");
    await page.keyboard.press("ArrowDown");
    assert.equal(await activeId(page), "target", "collapsed children are skipped");
    await page.keyboard.press("ArrowUp");
    await page.keyboard.press("ArrowRight");
    await page.keyboard.press("ArrowRight");
    assert.equal(await activeId(page), "moving");
    await page.keyboard.press("ArrowLeft");
    assert.equal(await activeId(page), "source");
    await page.keyboard.press("End");
    await page.keyboard.press("Space");
    await page.keyboard.press("Enter");
    assert.equal(await selectedId(page), "target");
    await page.keyboard.press("Alt+ArrowUp");
    assert.deepEqual(await page.locator(".outline-row").evaluateAll((nodes) => nodes.map((n) => n.dataset.nodeId)), ["page", "target", "source", "moving"]);
    assert.equal(await activeId(page), "target", "reorder preserves focus");
    await page.keyboard.press("Control+z");
    assert.equal(await activeId(page), "target");
    await row(page, "moving").click();
    await row(page, "source").locator(".outline-toggle").click();
    assert.equal(await activeId(page), "source", "toggle restores row focus even if selection was hidden");
    assert.equal(await page.locator('.outline-row[tabindex="0"]').count(), 1);
    await page.locator("#outline-tab").focus();
    await page.keyboard.press("Tab");
    assert.equal(await activeId(page), "source");
    await row(page, "source").locator(".outline-toggle").click();
    await row(page, "moving").click();
    await row(page, "moving").click();
    assert.equal(await activeId(page), "moving", "clicking current selection still focuses it");
    const input = page.locator("#inspector input").first();
    // Inspector fields must retain native editing keys.
    assert(await input.count());
    await input.focus();
    await page.keyboard.press("ArrowLeft");
    assert.equal(await selectedId(page), "moving");
    console.log("Outline navigation, focus restoration and reorder passed");

    for (const definition of [...componentRegistry(), ...recipeRegistry()]) {
      await mount(page, fixture(), { activePanel: "components" });
      const attribute = definition.template ? "recipe-type" : "component-type";
      const palette = page.locator(`.palette-item[data-${attribute}="${definition.type}"]`);
      await dragTo(page, palette, canvasNode(page, "target"));
      assert.equal(await page.locator(".designer-empty:visible").count(), 0);
      const before = await geometry(page);
      assert.equal(await page.locator("#outline-tree .designer-node").count(), 0);
      if (definition.type === "settings-row") await page.screenshot({ path: path.join(output, "recipe-preview.png") });
      await page.mouse.up();
      sameGeometry(before, await geometry(page));
      await page.keyboard.press("Control+z");
      assert.equal(await canvasNode(page, "target").locator(".designer-empty").count(), 1);
      console.log(`Preview/drop geometry: ${definition.type}`);
    }

    await mount(page, fixture());
    await dragTo(page, row(page, "moving"), row(page, "target"));
    const before = await geometry(page);
    assert.equal(await canvasNode(page, "source").locator('[data-node-id="moving"]').count(), 0);
    assert.equal(await page.locator("#outline-tree .designer-node").count(), 0);
    await page.mouse.up();
    sameGeometry(before, await geometry(page));
    await page.keyboard.press("Control+z");
    await dragTo(page, row(page, "moving"), row(page, "target"));
    await page.evaluate(() => document.dispatchEvent(new PointerEvent("pointercancel", { bubbles: true })));
    await page.mouse.up();
    assert.equal(await canvasNode(page, "source").locator('[data-node-id="moving"]').count(), 1, "cancel does not commit");
    assert.equal(await page.locator(".designer-drop-preview").count(), 0);

    // Same-parent before/after reorder must use the same adjusted index in preview.
    for (const layout of ["column", "row"]) {
      for (const after of [false, true]) {
        const model = fixture();
        Object.assign(model.root, { layout });
        model.root.children = ["a", "b", "c"].map((id) => ({ ...defaultNode("button", id), width: "80px" }));
        await mount(page, model);
        const from = after ? "a" : "c";
        const to = after ? "c" : "a";
        const fraction = layout === "row" ? { x: after ? 0.9 : 0.1, y: 0.5 } : { x: 0.5, y: after ? 0.9 : 0.1 };
        await dragTo(page, row(page, from), canvasNode(page, to), fraction);
        const projected = await geometry(page);
        assert.deepEqual(projected.slice(1).map((node) => node.id), after ? ["b", "c", "a"] : ["c", "a", "b"]);
        await page.mouse.up();
        sameGeometry(projected, await geometry(page));
        await page.keyboard.press("Control+z");
        assert.deepEqual((await geometry(page)).slice(1).map((node) => node.id), ["a", "b", "c"]);
        await page.keyboard.press("Control+y");
        sameGeometry(projected, await geometry(page));
      }
    }

    // Inline contenteditable text must not dispatch editor deletion commands.
    const textModel = fixture();
    textModel.root.children = [{ ...defaultNode("text", "editable"), text: "Hello" }];
    await mount(page, textModel);
    await canvasNode(page, "editable").click();
    await canvasNode(page, "editable").focus();
    await page.keyboard.press("End");
    await page.keyboard.press("Backspace");
    assert.equal(await canvasNode(page, "editable").count(), 1);

    const large = fixture();
    let parent = large.root;
    for (let level = 0; level < 6; level++) {
      const node = defaultNode("container", `nested-${level}`);
      node.children = [];
      parent.children.push(node);
      parent = node;
    }
    for (let i = 0; i < 30; i++) parent.children.push(defaultNode("text", `label-${i}`));
    for (const [width, height] of [[1280, 720], [1440, 900], [720, 720]]) {
      await page.setViewportSize({ width, height });
      await mount(page, large, { rightCollapsed: width < 1000 });
      await row(page, "page").focus();
      for (let i = 0; i < 39; i++) await page.keyboard.press("ArrowDown");
      assert.equal(await activeId(page), "label-29");
      await page.screenshot({ path: path.join(output, `outline-${width}x${height}.png`) });
    }
    await page.emulateMedia({ colorScheme: "light" });
    await page.addStyleTag({ content: ':root { --surface: #f3f3f3; --canvas-bg: #fff; --text: #222; --muted: #555; --input: #fff; --input-text: #222; --border: #ccc; --selection: #cee5f7; --hover: #e6e6e6; }' });
    await page.screenshot({ path: path.join(output, "outline-light.png") });
    assert.deepEqual(errors, []);
    console.log("Visual editor browser interaction tests passed");
  } finally {
    await browser.close();
  }
}
main().catch((error) => { console.error(error); process.exitCode = 1; });
