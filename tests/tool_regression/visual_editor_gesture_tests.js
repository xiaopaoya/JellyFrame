"use strict";

const assert = require("assert");
const {
  defaultGestureModel,
  classifySwipe,
  resolvePageIndex
} = require("../../tools/vscode-jellyframe/visual_editor_model");

const gesture = { swipe: { ...defaultGestureModel().swipe, enabled: true } };
assert.equal(classifySwipe(gesture, -29, 0).accepted, false);
assert.equal(classifySwipe(gesture, -40, 20).accepted, true);
assert.equal(classifySwipe(gesture, -40, 20).direction, "next");
assert.equal(classifySwipe(gesture, 40, 2).direction, "previous");
assert.equal(classifySwipe(gesture, -40, 0, { childActive: true }).reason, "child-control");
assert.equal(resolvePageIndex(0, "previous", 3, "clamp"), 0);
assert.equal(resolvePageIndex(2, "next", 3, "clamp"), 2);
assert.equal(resolvePageIndex(0, "previous", 3, "wrap"), 2);
assert.equal(resolvePageIndex(2, "next", 3, "wrap"), 0);
console.log("Visual-editor gesture contract tests passed");
