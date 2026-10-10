"use strict";

// Deterministic desktop replay for the model-level page/gesture contract.
// Runtime and author JavaScript remain outside this harness by design.
const {
  defaultGestureModel,
  classifySwipe,
  resolvePageIndex
} = require("../../tools/vscode-jellyframe/visual_editor_model");

const gesture = { swipe: { ...defaultGestureModel().swipe, enabled: true } };
const pages = ["page-1", "page-2", "page-3"];
let index = 0;
const events = [];

function replay(name, dx, dy, options = {}) {
  const before = pages[index];
  const result = classifySwipe(gesture, dx, dy, options);
  let transitioned = false;
  if (result.accepted) {
    const nextIndex = resolvePageIndex(index, result.direction, pages.length, gesture.swipe.edgeBehavior);
    transitioned = nextIndex !== index;
    index = nextIndex;
  }
  const event = { name, before, after: pages[index], transitioned, ...result };
  events.push(event);
  return event;
}

replay("below-threshold", -20, 0);
replay("vertical-cancel", -40, 40);
replay("next", -40, 0);
replay("child-priority", -40, 0, { childActive: true });
replay("previous", 40, 0);
replay("clamp-at-first", 40, 0);

if (events[0].accepted || events[1].accepted || events[3].accepted) throw new Error("cancelled gesture was accepted");
if (!events[2].transitioned || !events[4].transitioned || events[5].transitioned) throw new Error("page transition mismatch");

process.stdout.write(`${JSON.stringify({ contract: "visual-editor-swipe-v1", pages, finalPage: pages[index], events }, null, 2)}\n`);
