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
    commands: [{ type: "Text", nodeId: "title", us: 1000, pixels: 20 }]
  };
  const parsed = parseRenderTrace(`${JSON.stringify(session)}\n${JSON.stringify(frame)}\n`);
  assert.equal(parsed.frames.length, 1);
  assert.equal(parsed.errors.length, 0);
  const html = renderTraceHtml(parsed, true, "trace.jsonl", {
    cspSource: "vscode-resource:",
    frameImages: { "0": "vscode-resource://frame_000.bmp" }
  });
  assert(html.includes("frameSlider"));
  assert(html.includes("title"));
  assert(html.includes("timingComplete"));
  assert(html.includes("vscode-resource://frame_000.bmp"));
  assert(html.includes("dirtyCoverage"));
  assert(html.includes("img-src vscode-resource:"));

  const invalid = parseRenderTrace(`${JSON.stringify(session)}\n${JSON.stringify({ ...frame, frame: 2 })}\n${JSON.stringify({ ...frame, frame: 1 })}\n`);
  assert.equal(invalid.frames.length, 1);
  assert(invalid.errors.some((error) => error.includes("strictly increasing")));
}

main();
