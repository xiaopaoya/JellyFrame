"use strict";

// Compares the editor's generated source against the standard browser layout
// and the Native Runtime at the same viewport. The test deliberately compares
// alignment anchors, not font raster dimensions, because the embedded font
// backend is allowed to use different glyph metrics.
const assert = require("assert/strict");
const fs = require("fs");
const path = require("path");
const { spawnSync } = require("child_process");
const { chromium } = require("playwright");
const { createDefaultModel, defaultNode, renderBody, renderCss } = require("../../tools/vscode-jellyframe/visual_editor_model");

const shell = path.resolve(process.argv[2]);
assert(fs.existsSync(shell), `Native Runtime shell not found: ${shell}`);
const output = path.resolve("build/test_outputs/visual_editor/alignment-conformance");
const types = ["button", "text", "list"];
const horizontalValues = ["left", "center", "right"];
const verticalValues = ["start", "center", "end"];

function makeModel(type, horizontal, vertical) {
  const model = createDefaultModel({ width: 300, height: 300, shape: "rect" });
  model.root.padding = 0;
  model.root.paddingX = 0;
  model.root.paddingY = 0;
  model.root.gap = 0;
  model.root.justify = "start";
  const node = {
    ...defaultNode(type, "alignment-target"),
    text: "Align",
    items: ["Align"],
    width: "220px",
    height: "70px",
    itemHeight: 70,
    radius: 0,
    background: "transparent",
    paddingX: 0,
    paddingY: 0,
    fontSize: 16
  };
  if (type === "text") node.align = horizontal;
  else node.textAlign = horizontal;
  node.verticalAlign = vertical;
  model.root.children = [node];
  return model;
}

async function browserMetrics(page, model, type) {
  await page.setContent(`<style>${renderCss()}</style>${renderBody(model)}`);
  return page.evaluate((nodeType) => {
    function measure(element) {
      const walker = document.createTreeWalker(element, NodeFilter.SHOW_TEXT);
      let textNode = walker.nextNode();
      while (textNode && !textNode.textContent.trim()) textNode = walker.nextNode();
      if (!textNode) return undefined;
      const range = document.createRange();
      range.selectNodeContents(textNode);
      const text = range.getBoundingClientRect();
      const box = element.getBoundingClientRect();
      return {
        element: { x: box.x, y: box.y, width: box.width, height: box.height },
        text: { x: text.x, y: text.y, width: text.width, height: text.height },
        justify: getComputedStyle(element).justifyContent,
        display: getComputedStyle(element).display
      };
    }
    const element = document.getElementById("alignment-target");
    const target = nodeType === "list" ? element.querySelector("li") : element;
    return { root: measure(element), text: measure(target) };
  }, type);
}

function nativeMetrics(model, name) {
  const root = path.join(output, name);
  fs.mkdirSync(root, { recursive: true });
  fs.writeFileSync(path.join(root, "jellyframe.app.json"), JSON.stringify({
    format: "jellyframe.app", formatVersion: 0, id: "org.jellyframe.editor-alignment", name,
    version: { name: "0.1.0", code: 1 }, entry: "/index.html",
    runtime: { minJellyFrame: "0.6.0", minRenderCore: "0.6.2", script: "none" },
    viewport: { designWidth: 300, designHeight: 300, shape: "rect" }
  }));
  fs.writeFileSync(path.join(root, "index.html"), `<!doctype html><html><head><link rel="stylesheet" href="app.css"></head><body>${renderBody(model)}</body></html>`);
  fs.writeFileSync(path.join(root, "app.css"), renderCss());
  const trace = path.join(root, "trace.jsonl");
  const result = spawnSync(shell, ["--app", root, "--capture-frames", path.join(root, "frames"), "--frame-count", "1", "--render-trace", trace], { encoding: "utf8", timeout: 20000 });
  assert.equal(result.status, 0, `${name}: ${result.error || result.stderr || result.stdout}`);
  const frame = fs.readFileSync(trace, "utf8").split(/\r?\n/).filter(Boolean).map(JSON.parse).find((record) => record.type === "frame");
  const text = frame?.commandSpans?.find((command) => command.type === "Text");
  assert(text, `${name}: Native Runtime did not emit a text command`);
  return text.rect;
}

function assertNear(actual, expected, tolerance, label) {
  assert(Math.abs(actual - expected) <= tolerance, `${label}: actual=${actual} expected=${expected} tolerance=${tolerance}`);
}

(async () => {
  const browser = await chromium.launch({ channel: process.env.JELLYFRAME_TEST_BROWSER || "msedge", headless: true });
  try {
    const page = await browser.newPage({ viewport: { width: 300, height: 300 } });
    for (const type of types) {
      for (const horizontal of horizontalValues) {
        for (const vertical of verticalValues) {
          const model = makeModel(type, horizontal, vertical);
          const browserResult = await browserMetrics(page, model, type);
          const native = nativeMetrics(model, `${type}-${horizontal}-${vertical}`);
          const expected = browserResult.text.text;
          assertNear(native.x, expected.x, 6, `${type}/${horizontal}/${vertical} horizontal anchor`);
          assertNear(native.y, expected.y, 4, `${type}/${horizontal}/${vertical} vertical anchor`);
          if (type === "button" || type === "text") {
            assert.equal(browserResult.root.display, "flex", `${type} uses deterministic flex text box`);
          }
          if (type === "button" && vertical === "center") {
            assert.equal(browserResult.root.justify, "center", "button default center maps to standard justify-content");
          }
        }
      }
      const legacy = makeModel(type, "center", "center");
      delete legacy.root.children[0].verticalAlign;
      if (type === "text") delete legacy.root.children[0].align;
      else delete legacy.root.children[0].textAlign;
      const legacyBrowser = await browserMetrics(page, legacy, type);
      const legacyNative = nativeMetrics(legacy, `${type}-legacy-default`);
      assertNear(legacyNative.x, legacyBrowser.text.text.x, 6, `${type}/legacy default horizontal anchor`);
      assertNear(legacyNative.y, legacyBrowser.text.text.y, 4, `${type}/legacy default vertical anchor`);
    }
    console.log("Visual editor alignment conformance passed: browser baseline matches Native Runtime anchors");
  } finally {
    await browser.close();
  }
})().catch((error) => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
