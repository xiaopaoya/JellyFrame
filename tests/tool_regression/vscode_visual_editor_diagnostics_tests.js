"use strict";

const assert = require("assert");
const {
  attributeDiagnostic,
  detailAttributes,
  sourceLocationForId
} = require("../../tools/vscode-jellyframe/visual_editor_diagnostics");

const model = {
  root: {
    id: "page",
    children: [
      { id: "primary-button", type: "button" },
      { id: "title", type: "text" }
    ]
  }
};
const source = '<section id="page">\n  <button id="primary-button" class="jf-visual-button">Start</button>\n</section>\n';

const attributes = detailAttributes('node="button#primary-button.jf-visual-button" path="body>section#page>button#primary-button" boxWidth=32');
assert.equal(attributes.node, "button#primary-button.jf-visual-button");
assert.equal(attributes.path, "body>section#page>button#primary-button");
assert.equal(attributes.boxWidth, undefined, "only quoted diagnostic attributes are parsed");

const attributed = attributeDiagnostic({
  stage: "layout",
  code: "layout-text-overflow",
  detail: 'node="button#primary-button.jf-visual-button" path="body>section#page>button#primary-button"'
}, model, source);
assert.equal(attributed.nodeId, "primary-button");
assert.equal(attributed.propertyGroup, "layout");
assert.equal(attributed.sourceLocation.line, 1);
assert(attributed.sourceLocation.character > 0);
assert.equal(attributed.attributed, true);

const unowned = attributeDiagnostic({
  stage: "layout",
  code: "layout-text-overflow",
  detail: 'node="button.primary" path="body>main>button.primary"'
}, model, source);
assert.equal(unowned.nodeId, undefined);
assert.equal(unowned.propertyGroup, undefined);
assert.equal(unowned.attributed, false);
assert.match(unowned.reason, /does-not-identify/);

const ambiguous = attributeDiagnostic({
  stage: "layout",
  code: "layout-text-overflow",
  detail: 'path="body>section#page>button#primary-button" node="button#title"'
}, model, source);
assert.equal(ambiguous.nodeId, undefined, "multiple stable IDs must not be guessed");

assert.equal(sourceLocationForId(source, "missing"), undefined);
console.log("VS Code visual-editor diagnostics tests passed");
