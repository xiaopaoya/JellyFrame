"use strict";

const assert = require("assert");
const {
  BODY_START,
  CSS_START,
  createDefaultModel,
  defaultNode,
  migrateModel,
  nextId,
  renderBody,
  renderCss,
  recipeRegistry,
  updateCss,
  updateHtml,
  validateModel
} = require("../../tools/vscode-jellyframe/visual_editor_model");

const model = createDefaultModel({ width: 172, height: 320, shape: "rect" }, "Visual Test");
assert.equal(model.viewport.width, 172);
assert.equal(model.viewport.height, 320);
assert.equal(nextId(model, "text"), "text-1");
const registry = require("../../tools/vscode-jellyframe/visual_editor_model").componentRegistry();
assert(registry.every((component) => component.renderKey && component.fields.every((field) => field.group && field.kind)), "registry fields and renderers must be declared");
assert(registry.find((component) => component.type === "button").fields.some((field) => field.key === "height" && field.kind === "length"));
const recipes = recipeRegistry();
assert.equal(recipes.length, 3);
recipes.forEach((recipe) => {
  const recipeModel = createDefaultModel();
  recipeModel.root.children.push(recipe.template);
  validateModel(recipeModel);
});

const row = defaultNode("container", "content-row");
row.layout = "row";
row.children.push({ ...defaultNode("button", "confirm-button"), text: "Save & go" });
row.children.push({ ...defaultNode("text", "escaped-text"), text: "<safe> & readable" });
model.root.children.push(row);
validateModel(model);
assert.equal(migrateModel({ ...model, formatVersion: 1 }).formatVersion, 2);

const body = renderBody(model);
assert(body.includes(BODY_START));
assert(body.includes("flex-direction: row"));
assert(body.includes("&lt;safe&gt; &amp; readable"));
assert(body.includes("text-align: center"));
assert(body.includes("line-height: 24px"));
assert(body.includes("font-size: 16px"));

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
assert(!generatedCss.includes("line-height: inherit"));
assert(!generatedCss.includes("appearance"));
assert(!generatedCss.includes("border-radius: inherit"));
assert.equal(updateCss(generatedCss).split(CSS_START).length - 1, 1);

const defaultBody = renderBody(createDefaultModel());
assert(!defaultBody.includes("height: auto"));
assert(!defaultBody.includes("width: auto"));
assert(!defaultBody.includes("font: inherit"));
assert(!defaultBody.includes("line-height: inherit"));
assert(!defaultBody.includes("appearance"));

const invalid = createDefaultModel();
invalid.root.children.push(defaultNode("text", "page"));
assert.throws(() => validateModel(invalid), /Duplicate/);
assert.throws(() => validateModel({ ...model, formatVersion: 99 }), /Unsupported/);
assert.throws(() => validateModel({ ...createDefaultModel(), root: { ...createDefaultModel().root, children: [{ ...defaultNode("image", "remote-image"), src: "https://example.invalid/image.bmp" }] } }), /package-local resource/);
const invalidProgressModel = createDefaultModel();
invalidProgressModel.root.children.push({ ...defaultNode("progress", "bad-progress"), height: "50%" });
assert.throws(() => validateModel(invalidProgressModel), /pixel length/);
const invalidRegistryModel = createDefaultModel();
invalidRegistryModel.root.gap = "wide";
assert.throws(() => validateModel(invalidRegistryModel), /Invalid gap/);
const invalidColorModel = createDefaultModel();
invalidColorModel.root.background = "red; color: blue";
assert.throws(() => validateModel(invalidColorModel), /unsafe text value/);
const invalidEnumModel = createDefaultModel();
invalidEnumModel.root.layout = "diagonal";
assert.throws(() => validateModel(invalidEnumModel), /Invalid layout/);

const stage4 = createDefaultModel();
stage4.root.children.push(defaultNode("divider", "divider-1"));
stage4.root.children.push(defaultNode("spacer", "spacer-1"));
stage4.root.children.push(defaultNode("select", "select-1"));
stage4.root.children.push(defaultNode("list", "list-1"));
stage4.root.children.push(defaultNode("navigation", "navigation-1"));
stage4.root.children.push(defaultNode("switch", "switch-1"));
validateModel(stage4);
const stage4Body = renderBody(stage4);
assert(stage4Body.includes("<select id=\"select-1\""));
assert(stage4Body.includes("<ul id=\"list-1\""));
assert(stage4Body.includes("<nav id=\"navigation-1\""));
assert(stage4Body.includes("<button id=\"switch-1\""));
assert(stage4Body.includes("role=\"switch\""));
assert(stage4Body.includes("aria-checked=\"true\""));
assert(stage4Body.includes("height: 1px"));
assert(stage4Body.includes("height: 12px"));
assert(stage4Body.includes("font-size: 9px; line-height: 15px"));
assert(recipes.find((recipe) => recipe.type === "settings-row").template.children.some((node) => node.type === "switch"));
const bottomNavigationRecipe = recipes.find((recipe) => recipe.type === "bottom-navigation");
assert.equal(bottomNavigationRecipe.template.type, "container");
assert(bottomNavigationRecipe.template.children.some((node) => node.type === "list"));
assert(bottomNavigationRecipe.template.children.some((node) => node.type === "navigation"));
const stage4Css = renderCss();
assert(stage4Css.includes(".jf-visual-list li"));
assert(stage4Css.includes(".jf-visual-navigation { box-sizing: border-box; overflow: hidden; }"));
assert(stage4Css.includes(".jf-visual-navigation button { box-sizing: border-box;"));
assert(stage4Css.includes(".jf-visual-navigation button"));
const invalidItems = createDefaultModel();
invalidItems.root.children.push({ ...defaultNode("list", "invalid-list"), items: Array(9).fill("item") });
assert.throws(() => validateModel(invalidItems), /Invalid items/);
const invalidSelection = createDefaultModel();
invalidSelection.root.children.push({ ...defaultNode("select", "invalid-select"), selected: 4 });
assert.throws(() => validateModel(invalidSelection), /selected option/);

console.log("VS Code visual-editor model tests passed");
