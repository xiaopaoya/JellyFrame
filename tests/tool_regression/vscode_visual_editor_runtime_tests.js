"use strict";

const assert = require("assert/strict");
const fs = require("fs");
const path = require("path");
const { spawnSync } = require("child_process");
const { createDefaultModel, defaultNode, recipeRegistry, instantiateRecipe, renderBody, renderCss } = require("../../tools/vscode-jellyframe/visual_editor_model");
const shell = path.resolve(process.argv[2]);
const output = path.resolve("build/test_outputs/visual_editor/runtime");

function capture(name, model, extra = []) {
  const root = path.join(output, name);
  fs.mkdirSync(root, { recursive: true });
  fs.writeFileSync(path.join(root, "jellyframe.app.json"), JSON.stringify({
    format: "jellyframe.app", formatVersion: 0, id: "org.jellyframe.editor-test", name,
    version: { name: "0.1.0", code: 1 }, entry: "/index.html",
    runtime: { minJellyFrame: "0.6.0", minRenderCore: "0.6.2", script: "none" },
    viewport: { designWidth: model.viewport.width, designHeight: model.viewport.height, shape: model.viewport.shape }
  }));
  fs.writeFileSync(path.join(root, "index.html"), `<!doctype html><html><head><link rel="stylesheet" href="app.css"></head><body>${renderBody(model)}</body></html>`);
  fs.writeFileSync(path.join(root, "app.css"), renderCss());
  const result = spawnSync(shell, ["--app", root, "--capture-frames", path.join(root, "frames"),
    "--frame-count", "1", "--render-trace", path.join(root, "trace.jsonl"), ...extra], { encoding: "utf8", timeout: 20000 });
  fs.writeFileSync(path.join(root, "runtime.log"), (result.stdout || "") + (result.stderr || ""));
  assert.equal(result.status, 0, `${name}: ${result.error || result.stderr || result.stdout}`);
  assert(!/diagnostics: [1-9]|warning|error/i.test((result.stdout || "") + (result.stderr || "")), `${name}: ${result.stdout} ${result.stderr}`);
  console.log(`Native editor fixture passed: ${name}`);
  return root;
}

for (const viewport of [{ width: 172, height: 320, shape: "rect" }, { width: 300, height: 300, shape: "round" }, { width: 320, height: 240, shape: "rect" }]) {
  for (const recipe of recipeRegistry().filter((item) => item.group === "wearableGroup")) {
    const model = createDefaultModel(viewport);
    model.root = { ...instantiateRecipe(recipe, viewport), id: "page" };
    capture(`${recipe.type}-${viewport.width}x${viewport.height}`, model);
  }
}
for (const type of ["button", "text", "list"]) {
  let previousY;
  for (const verticalAlign of ["start", "center", "end"]) {
    const model = createDefaultModel({ width: 300, height: 300 });
    model.root.justify = "start";
    model.root.children = ["left", "center", "right"].map((align, i) => ({
      ...defaultNode(type, `align-${i}`), text: "Align", items: ["Align"], align, textAlign: align,
      width: "100%", height: "70px", itemHeight: 70, verticalAlign, paddingX: 6, paddingY: 3, fontSize: 16
    }));
    const root = capture(`${type}-${verticalAlign}`, model);
    const frame = fs.readFileSync(path.join(root, "trace.jsonl"), "utf8").trim().split(/\r?\n/).map(JSON.parse).find((item) => item.type === "frame");
    const text = frame.commandSpans.filter((item) => item.type === "Text");
    assert.equal(text.length, 3);
    assert(text[0].rect.x < text[1].rect.x && text[1].rect.x < text[2].rect.x, `${type}: horizontal positions must follow alignment`);
    if (previousY !== undefined) assert(text[0].rect.y > previousY, `${type}: vertical alignment must change actual paint coordinates`);
    previousY = text[0].rect.y;
  }
}
const scrolling = createDefaultModel({ width: 172, height: 160 });
scrolling.root = { ...structuredClone(recipeRegistry().find((item) => item.type === "function-list").template), id: "page" };
const scrollRoot = capture("function-list-scroll", scrolling, ["--frame-count", "3", "--frame-event", "1:wheel:80:80:-120"]);
assert(/scroll_containers scrolls=[1-9]/.test(fs.readFileSync(path.join(scrollRoot, "runtime.log"), "utf8")), "function list must actually scroll in Runtime");
console.log("Native visual editor runtime tests passed");
