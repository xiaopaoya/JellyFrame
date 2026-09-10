const assert = require("assert");
const { parseRenderTrace, renderTraceHtml } = require("../../tools/vscode-jellyframe/render_trace_viewer");

function main() {
  const session = { format: "jellyframe.render.trace.v0", type: "session", viewport: { width: 172, height: 320 } };
  const frame = {
    format: "jellyframe.render.trace.v0",
    type: "frame",
    frame: 0,
    totalUs: 2000,
    timingComplete: false,
    stagesUs: { layout: 500, paint: 1000 },
    dirtyRectCount: 1,
    dirtyAreaPercent: 3,
    dirtyRects: [
      { x: 8, y: 12, width: 64, height: 28 },
      { x: "invalid", y: 0, width: 10, height: 10 }
    ],
    dirtyRectsTruncated: true,
    commands: [
      { type: "Text", owner: "id:title", us: 1000, pixels: 20, samples: 2 },
      { type: "FillRect", owner: "n2", us: 400, pixels: 50, samples: 1 },
      { type: "Text", owner: "ignored", us: -1, pixels: 1, samples: 1 }
    ],
    commandsTruncated: true,
    nodesTruncated: true,
    commandInvalidSamples: 1
  };
  const parsed = parseRenderTrace(`${JSON.stringify(session)}\n${JSON.stringify(frame)}\n`);
  assert.equal(parsed.frames.length, 1);
  assert.equal(parsed.errors.length, 0);
  const html = renderTraceHtml(parsed, true, "trace.jsonl", {
    cspSource: "vscode-resource:",
    frameImages: { "0": "vscode-resource://frame_000.bmp" }
  });
  assert(html.includes("frameSlider"));
  assert(html.includes("id:title"));
  assert(html.includes("timingComplete"));
  assert(html.includes("vscode-resource://frame_000.bmp"));
  assert(html.includes("dirtyCoverage"));
  assert(html.includes("dirtyRects"));
  assert(html.includes('"width":64'));
  assert(html.includes("dirtyRectsTruncated"));
  assert(html.includes("capture-stage"));
  assert(html.includes("dirty-overlay"));
  assert(html.includes("commandsTruncated"));
  assert(html.includes("nodesTruncated"));
  assert(html.includes("commandInvalidSamples"));
  assert(html.includes("item.owner||item.nodeId"));
  assert(!html.includes("ignored</code>"));
  assert(!html.includes("invalid,0 10x10"));
  assert(html.includes("img-src vscode-resource:"));

  const invalid = parseRenderTrace(`${JSON.stringify(session)}\n${JSON.stringify({ ...frame, frame: 2 })}\n${JSON.stringify({ ...frame, frame: 1 })}\n`);
  assert.equal(invalid.frames.length, 1);
  assert(invalid.errors.some((error) => error.includes("strictly increasing")));
}

main();
