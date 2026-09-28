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
const { createDefaultModel, defaultNode, componentRegistry, recipeRegistry, instantiateRecipe, validateModel, renderBody } = require("../../tools/vscode-jellyframe/visual_editor_model");
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

async function mount(page, model, state = {}, assets = {}) {
  const html = visualEditorHtml({ cspSource: "", asWebviewUri: (uri) => uri }, "interaction-test", model, assets)
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
const handle = (page, edge) => page.locator(`.selection-overlay [data-resize-edge="${edge}"]`);
async function resizeBy(page, edge, dx, dy, end = "up") {
  const box = await handle(page, edge).evaluate((item) => {
    const { x, y, width, height } = item.getBoundingClientRect();
    return { x, y, width, height };
  });
  assert(box, `visible ${edge} handle`);
  const x = box.x + box.width / 2;
  const y = box.y + box.height / 2;
  await page.mouse.move(x, y);
  await page.mouse.down();
  await page.mouse.move(x + dx, y + dy, { steps: 4 });
  if (end === "return") await page.mouse.move(x, y, { steps: 4 });
  if (end === "escape") await page.keyboard.press("Escape");
  if (end === "cancel") await page.evaluate(() => document.dispatchEvent(new PointerEvent("pointercancel", { pointerId: 1, bubbles: true })));
  if (end === "blur") await page.evaluate(() => window.dispatchEvent(new Event("blur")));
  await page.mouse.up();
}
async function savedModel(page) {
  await page.locator("#save").click();
  const model = await page.evaluate(() => window.messages.filter((item) => item.type === "save").at(-1).model);
  validateModel(model);
  assert(!/resize-handle|selection-overlay/.test(renderBody(model)));
  await page.evaluate(() => window.dispatchEvent(new MessageEvent("message", { data: { type: "saved" } })));
  return model;
}
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

    // Rebuilding the design DOM must not reset nested scrolling viewports.
    const scrolling = fixture();
    const inner = { ...defaultNode("container", "inner-scroll"), height: "160px", padding: 4, gap: 4, overflowY: "auto",
      children: Array.from({ length: 12 }, (_, i) => ({ ...defaultNode("button", `scroll-item-${i}`), height: "40px" })) };
    const outer = { ...defaultNode("container", "outer-scroll"), height: "260px", padding: 4, gap: 4, overflowY: "auto",
      children: [{ ...defaultNode("container", "scroll-header"), height: "200px", children: [] }, inner,
        { ...defaultNode("container", "scroll-footer"), height: "200px", children: [] }] };
    scrolling.root.children = [outer];
    await mount(page, scrolling);
    const scrollOffsets = () => page.locator('#canvas [data-node-id$="-scroll"]').evaluateAll((nodes) =>
      nodes.map((node) => [node.dataset.nodeId, node.scrollTop, node.scrollLeft]));
    await canvasNode(page, "outer-scroll").evaluate((node) => { node.scrollTop = 160; });
    await canvasNode(page, "inner-scroll").evaluate((node) => { node.scrollTop = 176; });
    const savedOffsets = await scrollOffsets();
    assert(savedOffsets.every(([, top]) => top > 0), "fixture scrolls both containers");
    await canvasNode(page, "scroll-item-5").click();
    assert.equal(await selectedId(page), "scroll-item-5");
    assert.deepEqual(await scrollOffsets(), savedOffsets, "canvas selection preserves nested scrolling");
    await row(page, "scroll-item-6").click();
    assert.deepEqual(await scrollOffsets(), savedOffsets, "outline selection preserves canvas scrolling");
    const fontField = page.locator('#inspector .field').filter({ has: page.locator('label', { hasText: 'Font size' }) });
    await fontField.locator("input").fill("18");
    await fontField.locator("input").press("Tab");
    assert.deepEqual(await scrollOffsets(), savedOffsets, "property redraw preserves scrolling");
    await page.locator("#undo").click();
    assert.deepEqual(await scrollOffsets(), savedOffsets, "undo preserves scrolling");
    await page.locator("#redo").click();
    assert.deepEqual(await scrollOffsets(), savedOffsets, "redo preserves scrolling");
    await canvasNode(page, "scroll-item-5").click({ button: "right" });
    assert.deepEqual(await scrollOffsets(), savedOffsets, "context-menu selection preserves scrolling");
    await page.keyboard.press("Escape");
    await page.screenshot({ path: path.join(output, "nested-scroll-selection.png") });
    await page.locator("#components-tab").click();
    await dragTo(page, page.locator('[data-component-type="button"]'), canvasNode(page, "scroll-item-5"));
    assert.deepEqual(await scrollOffsets(), savedOffsets, "drop projection preserves scrolling");
    await page.mouse.up();
    assert.deepEqual(await scrollOffsets(), savedOffsets, "committed drop preserves scrolling");
    await page.locator("#undo").click();
    assert.deepEqual(await scrollOffsets(), savedOffsets);
    await dragTo(page, page.locator('[data-component-type="button"]'), canvasNode(page, "scroll-item-5"));
    await page.evaluate(() => document.dispatchEvent(new PointerEvent("pointercancel", { bubbles: true })));
    await page.mouse.up();
    assert.deepEqual(await scrollOffsets(), savedOffsets, "cancelled projection restores original scrolling");
    await page.locator("#outline-tab").click();
    await row(page, "inner-scroll").click();
    await row(page, "inner-scroll").click();
    assert.equal(await selectedId(page), "inner-scroll");
    const heightField = page.locator('#inspector .field').filter({ has: page.locator('label', { hasText: /^Height$/ }) });
    await heightField.locator("input").fill("600");
    await heightField.locator("input").press("Tab");
    const clamped = await canvasNode(page, "inner-scroll").evaluate((node) =>
      ({ top: node.scrollTop, maximum: node.scrollHeight - node.clientHeight }));
    assert.equal(clamped.top, clamped.maximum, "offset clamps to the resized container's scroll range");
    assert.equal(clamped.top, 0, "selection chrome does not extend the scroll range");
    await page.locator("#undo").click();
    // The last viewport position, not an outdated scroll snapshot, is retained.
    assert.equal(await canvasNode(page, "inner-scroll").evaluate((node) => node.scrollTop), clamped.top);
    console.log("Nested canvas scroll retention passed");

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

    const alignment = fixture();
    alignment.root.children = [{ ...defaultNode("button", "aligned"), height: "100px" }];
    await mount(page, alignment);
    await row(page, "aligned").click();
    const field = (label) => page.locator("#inspector .field").filter({ has: page.locator("label", { hasText: label }) });
    assert.equal(await field("Vertical content").locator("button.active").textContent(), "Center");
    assert.equal(await field("Horizontal padding").locator("input").inputValue(), "12");
    await field("Horizontal content").getByRole("button", { name: "Right", exact: true }).click();
    await field("Vertical content").getByRole("button", { name: "Bottom", exact: true }).click();
    await field("Font size").locator("input").fill("24");
    await field("Font size").locator("input").press("Tab");
    const style = await canvasNode(page, "aligned").evaluate((node) => {
      const s = getComputedStyle(node);
      return [s.textAlign, s.justifyContent, s.fontSize];
    });
    assert.deepEqual(style, ["right", "flex-end", "24px"]);
    await page.screenshot({ path: path.join(output, "content-alignment.png") });

    const advancedModel = fixture();
    advancedModel.root.children = [{ ...defaultNode("text", "advanced-text"), text: "Long wearable status", width: "120px", height: "40px" }];
    await mount(page, advancedModel);
    await row(page, "advanced-text").click();
    const advancedField = (label) => page.locator("#inspector .field").filter({ has: page.locator("label", { hasText: label }) });
    await advancedField("Line height").locator("input").fill("26");
    await advancedField("Line height").locator("input").press("Tab");
    await advancedField("Wrapping").locator("select").selectOption("nowrap");
    await advancedField("Text overflow").locator("select").selectOption("ellipsis");
    await advancedField("Border width").locator("input").fill("2");
    await advancedField("Border width").locator("input").press("Tab");
    await advancedField("Border color").locator("input[type=text]").fill("#40d49a");
    await advancedField("Border color").locator("input[type=text]").press("Tab");
    const advancedStyle = await canvasNode(page, "advanced-text").evaluate((node) => {
      const style = getComputedStyle(node);
      return { lineHeight: style.lineHeight, whiteSpace: style.whiteSpace, overflow: style.overflow,
        textOverflow: style.textOverflow, borderWidth: style.borderWidth, borderColor: style.borderColor };
    });
    assert.deepEqual(advancedStyle, { lineHeight: "26px", whiteSpace: "nowrap", overflow: "hidden",
      textOverflow: "ellipsis", borderWidth: "2px", borderColor: "rgb(64, 212, 154)" });
    const advancedSave = await savedModel(page);
    assert.equal(advancedSave.root.children[0].lineHeight, 26);
    assert.equal(advancedSave.root.children[0].whiteSpace, "nowrap");
    assert.equal(advancedSave.root.children[0].textOverflow, "ellipsis");
    assert.equal(advancedSave.root.children[0].borderWidth, 2);
    await page.screenshot({ path: path.join(output, "advanced-text-fields.png") });

    // Canvas selection exposes editor-only resize handles. The root remains
    // protected, while percentage/auto dimensions become concrete px values
    // when a drag starts and survive the normal model-check path.
    const resizable = fixture();
    resizable.root.children = [{ ...defaultNode("button", "resizable"), width: "50%", height: "60px" }];
    await mount(page, resizable, { zoom: 1 });
    await row(page, "resizable").click();
    assert.equal(await page.locator(".selection-overlay [data-resize-edge]").count(), 8);
    await row(page, "page").click();
    assert.equal(await page.locator("[data-resize-edge]").count(), 0, "root selection has no resize handles");
    await row(page, "resizable").click();
    const widthBefore = (await canvasNode(page, "resizable").boundingBox()).width;
    const east = await handle(page, "e").boundingBox();
    await page.mouse.move(east.x + east.width / 2, east.y + east.height / 2);
    await page.mouse.down();
    await page.mouse.move(east.x + east.width / 2 + 24, east.y + east.height / 2, { steps: 4 });
    await page.mouse.up();
    const widthAfter = (await canvasNode(page, "resizable").boundingBox()).width;
    assert(widthAfter > widthBefore + 15, "east handle increases width");
    const exactWidthField = page.locator("#inspector .field").filter({ has: page.locator("label", { hasText: /^Width$/ }) });
    assert.equal(await exactWidthField.locator("select").inputValue(), "px", "resize materializes a px width");
    await page.keyboard.press("Control+z");
    assert.equal(await exactWidthField.locator("input").inputValue(), "50", "resize is undoable");
    assert.equal(await exactWidthField.locator("select").inputValue(), "%", "resize restores the original unit");

    await row(page, "resizable").click({ button: "right" });
    const menu = page.getByRole("menu");
    await menu.waitFor();
    assert.equal(await menu.getByRole("menuitem", { name: "Delete", exact: true }).isDisabled(), false);
    await menu.getByRole("menuitem", { name: "Copy stable ID", exact: true }).click();
    assert.deepEqual(await page.evaluate(() => window.messages.at(-1)), { type: "copy-node-id", text: "resizable" });
    await canvasNode(page, "resizable").click({ button: "right" });
    await page.getByRole("menuitem", { name: "Show in outline", exact: true }).click();
    assert.equal(await page.locator("#outline-panel").isVisible(), true);
    assert.equal(await activeId(page), "resizable");
    await row(page, "page").click({ button: "right" });
    assert.equal(await page.getByRole("menuitem", { name: "Delete", exact: true }).isDisabled(), true);
    await page.keyboard.press("Escape");
    assert.equal(await page.getByRole("menu").count(), 0, "Escape closes the editor menu");
    console.log("Resize handles and context menu passed");

    // Every registry type, including real replaced images/inputs/selects, uses
    // external chrome and commits dimensions accepted by the source validator.
    const png = "data:image/png;base64," + fs.readFileSync(path.join(editor, "media/jellyframe.png")).toString("base64");
    for (const definition of componentRegistry()) {
      const model = fixture();
      const node = { ...defaultNode(definition.type, "sized"), width: "100px" };
      if (node.type === "image") node.src = "/icon.png";
      model.root.children = [node];
      await mount(page, model, {}, { "/icon.png": png });
      await row(page, "sized").click();
      if (node.type === "image") assert.equal(await canvasNode(page, "sized").evaluate((item) => item.tagName), "IMG");
      assert.equal(await canvasNode(page, "sized").locator("[data-resize-edge]").count(), 0);
      const before = await canvasNode(page, "sized").boundingBox();
      if (node.type === "input" || node.type === "image") {
        await page.screenshot({ path: path.join(output, `resize-${node.type}.png`) });
      }
      await resizeBy(page, "se", 20, 20);
      const changed = await savedModel(page);
      const result = changed.root.children[0];
      assert.equal(result.width, "120px", definition.type);
      const heightField = definition.fields.find((item) => item.key === "height");
      if (heightField.kind === "number") {
        assert.equal(typeof result.height, "number");
        assert(result.height >= heightField.min && result.height <= heightField.max);
      } else assert.match(result.height, /^\d+px$/);
      if (node.type === "text") assert.equal(result.text, node.text);
      const after = await canvasNode(page, "sized").boundingBox();
      await mount(page, changed, {}, { "/icon.png": png });
      const reopened = await canvasNode(page, "sized").boundingBox();
      assert(Math.abs(reopened.width - after.width) < 0.1 && Math.abs(reopened.height - after.height) < 0.1,
        `${definition.type}: saved/reopened dimensions match`);
      assert(after.width > before.width);
    }
    for (const zoom of [0.5, 1, 1.5]) {
      for (const edge of ["n", "ne", "e", "se", "s", "sw", "w", "nw"]) {
        const model = fixture();
        model.root.children = [{ ...defaultNode("button", "sized"), width: "100px", height: "60px" }];
        await mount(page, model, { zoom });
        await row(page, "sized").click();
        const x = edge.includes("e") ? 20 : edge.includes("w") ? -20 : 0;
        const y = edge.includes("s") ? 20 : edge.includes("n") ? -20 : 0;
        await resizeBy(page, edge, x * zoom, y * zoom);
        const changed = await savedModel(page);
        assert.equal(changed.root.children[0].width, x ? "120px" : "100px", `${edge} at ${zoom}`);
        assert.equal(changed.root.children[0].height, y ? "80px" : "60px", `${edge} at ${zoom}`);
      }
    }
    for (const end of ["up", "return", "escape", "cancel", "blur"]) {
      await mount(page, resizable);
      await row(page, "resizable").click();
      await resizeBy(page, "se", end === "up" ? 0 : 25, end === "up" ? 0 : 25, end);
      assert.equal(await page.locator("body").getAttribute("data-save-state"), "ready");
      assert(await page.locator("#undo").isDisabled(), "no-op/cancel creates no history");
      assert.deepEqual(await savedModel(page), resizable, "cancel preserves units and model");
    }
    const autoSized = fixture();
    autoSized.root.layout = "row";
    autoSized.root.children = [{ ...defaultNode("text", "auto-sized"), text: "Size", width: "auto", height: "auto" }];
    await mount(page, autoSized, { zoom: 0.5 });
    await row(page, "auto-sized").click();
    const autoBefore = await canvasNode(page, "auto-sized").boundingBox();
    await resizeBy(page, "e", 10, 0);
    const autoResult = await savedModel(page);
    assert.equal(autoResult.root.children[0].width, `${Math.round(autoBefore.width / 0.5 + 20)}px`);
    assert.equal(autoResult.root.children[0].height, "auto", "only the dragged dimension converts to px");
    await page.locator("#undo").click();
    assert.deepEqual(await savedModel(page), autoSized);
    await mount(page, scrolling);
    await canvasNode(page, "outer-scroll").evaluate((item) => { item.scrollTop = 160; });
    await canvasNode(page, "inner-scroll").evaluate((item) => { item.scrollTop = 176; });
    await row(page, "inner-scroll").click();
    await resizeBy(page, "s", 0, 400, "escape");
    assert.deepEqual(await scrollOffsets(), savedOffsets, "cancelling a resize restores pre-preview scrolling");
    await mount(page, resizable);
    await row(page, "resizable").click();
    await resizeBy(page, "e", 20, 0);
    await page.locator("#undo").click();
    await resizeBy(page, "e", 20, 0, "escape");
    assert.equal(await page.locator("#redo").isDisabled(), false, "cancel preserves redo");
    await page.locator("#redo").click();
    const redone = await savedModel(page);
    assert.equal(redone.root.children[0].width, "172px");
    console.log("All node sizing, eight edges, three zooms, cancellation and reopen passed");

    await mount(page, fixture());
    await row(page, "page").focus();
    await page.keyboard.press("Shift+F10");
    assert.equal(await page.getByRole("menuitem", { name: "Duplicate", exact: true }).isDisabled(), true);
    await page.keyboard.press("End");
    assert.equal(await page.evaluate(() => document.activeElement.textContent), "Show in outline",
      "menu skips root-only disabled actions");
    await page.keyboard.press("ArrowDown");
    assert.equal(await page.evaluate(() => document.activeElement.textContent), "Select");
    await page.keyboard.press("ArrowDown");
    await page.keyboard.press("Enter");
    assert.deepEqual(await page.evaluate(() => window.messages.at(-1)), { type: "copy-node-id", text: "page" });
    assert.equal(await activeId(page), "page");
    await page.keyboard.press("Shift+F10");
    await page.keyboard.press("Escape");
    assert.equal(await activeId(page), "page");
    await page.keyboard.press("Shift+F10");
    await page.keyboard.press("Tab");
    assert.equal(await page.getByRole("menu").count(), 0);
    await row(page, "moving").click({ button: "right" });
    await page.screenshot({ path: path.join(output, "canvas-menu-keyboard.png") });
    await page.getByRole("menuitem", { name: "Duplicate", exact: true }).click();
    assert.equal(await page.locator("#canvas .jf-visual-button").count(), 2);
    await page.keyboard.press("Shift+F10");
    await page.getByRole("menuitem", { name: "Delete", exact: true }).click();
    assert.equal(await page.locator("#canvas .jf-visual-button").count(), 1);
    console.log("Keyboard context menu navigation and focus passed");

    for (const viewport of [{ width: 172, height: 320, shape: "rect" }, { width: 300, height: 300, shape: "round" }, { width: 320, height: 240, shape: "rect" }]) {
      for (const recipe of recipeRegistry().filter((item) => item.group === "wearableGroup")) {
        const model = createDefaultModel(viewport);
        model.root = { ...instantiateRecipe(recipe, viewport), id: "page" };
        await mount(page, model, { activePanel: "components" });
        assert.equal(await page.locator(".palette-group h3").first().textContent(), "Wearable recipes");
        await page.locator("#canvas-shell").screenshot({ path: path.join(output, `${recipe.type}-${viewport.width}x${viewport.height}.png`) });
        assert(await page.locator("#canvas .designer-node").evaluateAll((nodes) => nodes.every((node) => {
          const rect = node.getBoundingClientRect();
          return rect.width > 0 && rect.height > 0;
        })));
      }
    }

    const round = createDefaultModel({ width: 300, height: 300, shape: "round" });
    round.root.children = [];
    round.root.padding = 0;
    await mount(page, round, { activePanel: "components" });
    await page.locator('[data-recipe-type="function-list"]').click();
    const insets = await page.locator('#canvas .jf-visual-container > .jf-visual-container').first().evaluate((node) => {
      const style = getComputedStyle(node);
      return [style.paddingLeft, style.paddingTop];
    });
    assert.deepEqual(insets, ["44px", "44px"]);

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
