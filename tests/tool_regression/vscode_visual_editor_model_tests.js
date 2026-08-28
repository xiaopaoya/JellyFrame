"use strict";

const assert = require("assert");
const {
  BODY_START,
  CSS_START,
  createDefaultModel,
  defaultNode,
  nextId,
  renderBody,
  updateCss,
  updateHtml,
  validateModel
} = require("../../tools/vscode-jellyframe/visual_editor_model");

const model = createDefaultModel({ width: 172, height: 320, shape: "rect" }, "Visual Test");
assert.equal(model.viewport.width, 172);
assert.equal(model.viewport.height, 320);
assert.equal(nextId(model, "text"), "text-1");

const row = defaultNode("container", "content-row");
row.layout = "row";
row.children.push({ ...defaultNode("button", "confirm-button"), text: "Save & go" });
row.children.push({ ...defaultNode("text", "escaped-text"), text: "<safe> & readable" });
model.root.children.push(row);
validateModel(model);

const body = renderBody(model);
assert(body.includes(BODY_START));
assert(body.includes("flex-direction: row"));
assert(body.includes("&lt;safe&gt; &amp; readable"));

const originalHtml = "<!doctype html>\n<html><head><title>Keep</title></head><body><main>Old</main><script src=\"scripts/app.js\"></script></body></html>\n";
const generatedHtml = updateHtml(originalHtml, model);
assert(generatedHtml.includes("<title>Keep</title>"));
assert(generatedHtml.includes("confirm-button"));
assert(!generatedHtml.includes("<main>Old</main>"));
assert(generatedHtml.includes("<script src=\"scripts/app.js\"></script>"));
assert.equal(updateHtml(generatedHtml, model).split(BODY_START).length - 1, 1);

const originalCss = ".hand-authored { color: red; }\n";
const generatedCss = updateCss(originalCss);
assert(generatedCss.startsWith(originalCss));
assert(!generatedCss.includes("\n+"));
assert(!generatedCss.includes("font: inherit"));
assert(!generatedCss.includes("border-radius: inherit"));
assert.equal(updateCss(generatedCss).split(CSS_START).length - 1, 1);

const defaultBody = renderBody(createDefaultModel());
assert(!defaultBody.includes("height: auto"));
assert(!defaultBody.includes("width: auto"));

const invalid = createDefaultModel();
invalid.root.children.push(defaultNode("text", "page"));
assert.throws(() => validateModel(invalid), /Duplicate/);
assert.throws(() => validateModel({ ...model, formatVersion: 99 }), /Unsupported/);
assert.throws(() => validateModel({ ...createDefaultModel(), root: { ...createDefaultModel().root, children: [defaultNode("image", "missing-image")] } }), /package-local resource/);
const invalidProgressModel = createDefaultModel();
invalidProgressModel.root.children.push({ ...defaultNode("progress", "bad-progress"), height: "50%" });
assert.throws(() => validateModel(invalidProgressModel), /pixel length/);

console.log("VS Code visual-editor model tests passed");
