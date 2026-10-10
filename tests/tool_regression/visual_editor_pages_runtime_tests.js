"use strict";

const assert = require("assert/strict");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const { spawnSync } = require("child_process");
const { createDefaultModel, defaultNode, renderBody, renderCss, validateModel, updateHtml } = require("../../tools/vscode-jellyframe/visual_editor_model");

const model = createDefaultModel();
model.root.children = [{ ...defaultNode("button", "forward"), text: "Next", pageAction: "next" }];
const second = { ...defaultNode("container", "details"), height: "100%", background: "#000000", children: [
  { ...defaultNode("button", "backward"), text: "Back", pageAction: "previous" }
] };
model.pageModel.pages.push({ id: "page-2", label: "Details", rootId: second.id, root: second });
model.gestureModel.swipe.enabled = true;
const body = renderBody(model);
assert(body.includes('id="forward"') && body.includes('id="backward"'));
assert.equal(updateHtml(`<body>${body}<script src="author.js"></script></body>`, model).match(/var config =/g).length, 1);
const duplicate = structuredClone(model);
duplicate.pageModel.pages[1].root.children[0].id = "forward";
assert.throws(() => validateModel(duplicate), /Duplicate/);

const script = /<script>([\s\S]*?)<\/script>/.exec(body)[1];
const listeners = {};
const visible = [true, false];
const pages = visible.map((_, i) => ({ classList: { toggle(name, hidden) { visible[i] = !hidden; } } }));
const context = { window: {}, document: {
  querySelectorAll() { return pages; },
  addEventListener(type, handler) { listeners[type] = handler; }
} };
vm.runInNewContext(script, context);
const api = context.window.JellyFramePages;
assert.equal(api.current(), "page-1");
assert.equal(api.next(), true);
assert.deepEqual(visible, [false, true]);
assert.equal(api.next(), false);
assert.equal(api.go("unknown"), false);
assert.equal(api.previous(), true);
const target = { closest() { return null; } };
function point(x, y) { return { clientX: x, clientY: y }; }
function drag(endX, endY, cancel = false) {
  listeners.pointerdown({ target, clientX: 100, clientY: 100 });
  if (cancel) listeners.mousemove({ buttons: 0, clientX: 100, clientY: 100 });
  listeners.pointerup({ clientX: endX, clientY: endY });
}
drag(80, 100);
assert.equal(api.current(), "page-1");
drag(50, 100, true);
assert.equal(api.current(), "page-1");
listeners.pointerdown({ target, clientX: 100, clientY: 100 });
listeners.mousemove({ buttons: 1, clientX: 100, clientY: 130 });
listeners.pointerup({ clientX: 40, clientY: 100 });
assert.equal(api.current(), "page-1", "vertical cancellation must survive a later horizontal move");
listeners.pointerdown({ target: { closest() { return {}; } }, clientX: 100, clientY: 100 });
listeners.pointerup({ clientX: 40, clientY: 100 });
assert.equal(api.current(), "page-1");
drag(40, 100);
assert.equal(api.current(), "page-2");

if (process.argv[2]) {
  const root = path.resolve("build/test_outputs/visual_editor/pages-runtime");
  fs.mkdirSync(root, { recursive: true });
  fs.writeFileSync(path.join(root, "jellyframe.app.json"), JSON.stringify({
    format: "jellyframe.app", formatVersion: 0, id: "org.jellyframe.pages-test", name: "Pages",
    version: { name: "0.1.0", code: 1 }, entry: "/index.html",
    runtime: { minJellyFrame: "0.6.0", minRenderCore: "0.6.2", script: "classic" },
    viewport: { designWidth: 300, designHeight: 300, shape: "rect" }
  }));
  fs.writeFileSync(path.join(root, "index.html"), `<html><head><link rel="stylesheet" href="app.css"></head><body>${body}</body></html>`);
  fs.writeFileSync(path.join(root, "app.css"), renderCss());
  fs.writeFileSync(path.join(root, "flow.jfcapture"), "frames 6\nstep-ms 100\nviewport 300 300\nevent 2 click-id forward\nevent 4 click-id backward\n");
  const result = spawnSync(path.resolve(process.argv[2]), ["--app", root, "--frame-script", path.join(root, "flow.jfcapture"),
    "--capture-frames", path.join(root, "frames"), "--render-trace", path.join(root, "trace.jsonl")], { encoding: "utf8", timeout: 30000 });
  fs.writeFileSync(path.join(root, "runtime.log"), (result.stdout || "") + (result.stderr || ""));
  assert.equal(result.status, 0, result.stdout + result.stderr);
  const frames = fs.readFileSync(path.join(root, "trace.jsonl"), "utf8").trim().split(/\r?\n/).map(JSON.parse).filter((item) => item.type === "frame");
  const owners = (frame) => frame.commandSpans.map((span) => span.owner);
  assert(owners(frames[0]).includes("id:forward"));
  assert(!owners(frames[0]).includes("id:backward"));
  assert(owners(frames.find((frame) => frame.frame === 2)).includes("id:backward"));
  assert(owners(frames.find((frame) => frame.frame === 4)).includes("id:forward"));
}
console.log("Visual editor page runtime tests passed");
