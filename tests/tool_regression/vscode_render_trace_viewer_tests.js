const assert = require("assert");
const fs = require("fs");
const { parseRenderTrace, aggregateTrace, renderTraceHtml } = require("../../tools/vscode-jellyframe/render_trace_viewer");
const vm = require("vm");

function loadTraceHelpers() {
  const source = fs.readFileSync(require.resolve("../../tools/vscode-jellyframe/extension"), "utf8");
  const module = { exports: {} };
  const sandbox = {
    Buffer,
    console,
    module,
    process,
    require(request) {
      if (request === "fs") return fs;
      if (request === "path") return require("path");
      if (request === "child_process") return {};
      if (request === "vscode") return {};
      if (request.startsWith("./")) return {};
      return require(request);
    }
  };
  vm.runInNewContext(`(function (require, module, exports) {\n${source}\n})(require, module, module.exports);`, sandbox);
  return module.exports;
}

const { traceDirectoryNames, traceFrameImageName } = loadTraceHelpers();

function main() {
  let directoryReads = 0;
  const availableNames = traceDirectoryNames("trace-directory", () => {
    directoryReads += 1;
    return ["capture.bmp", "frame_000.bmp", "frame_000.ppm", "frame_000.png"];
  });
  assert.equal(directoryReads, 1, "trace directory is read once for the image snapshot");
  assert.equal(
    traceFrameImageName({ frame: 0, captureFile: "nested/capture.bmp" }, availableNames),
    "capture.bmp",
    "captureFile keeps precedence over generated image names"
  );
  assert.equal(
    traceFrameImageName({ frame: 0 }, availableNames),
    "frame_000.bmp",
    "generated images retain bmp, ppm, png precedence"
  );
  assert.equal(
    traceFrameImageName({ frame: 1 }, availableNames),
    undefined,
    "missing frame images are ignored"
  );
  assert.doesNotThrow(() => traceDirectoryNames("missing-trace-directory", () => {
    throw new Error("directory unavailable");
  }));
  assert.equal(
    traceFrameImageName({ frame: 0 }, traceDirectoryNames("missing-trace-directory", () => {
      throw new Error("directory unavailable");
    })),
    undefined,
    "unreadable trace directories produce no frame images"
  );

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
      { type: "BoxShadow", nodeId: "legacy-card", us: 300, pixels: 9 },
      { type: "Image", us: 200, pixels: 4, samples: 1 },
      { type: "Text", owner: "ignored", us: -1, pixels: 1, samples: 1 }
    ],
    commandsTruncated: true,
    nodesTruncated: true,
    commandInvalidSamples: 1
  };
  const parsed = parseRenderTrace(`${JSON.stringify(session)}\n${JSON.stringify(frame)}\n`);
  assert.equal(parsed.frames.length, 1);
  assert.equal(parsed.errors.length, 0);
  const aggregate = aggregateTrace({ frames: [
    frame,
    { ...frame, frame: 1, totalUs: 3000, stagesUs: { layout: 700, paint: 1200 }, commands: [
      { type: "Text", owner: "id:title", us: 300, pixels: 20, samples: 3 },
      { type: "FillRect", us: 600, pixels: 50, samples: 2 },
      { type: "Image", us: 100, pixels: 4 }
    ], commandsTruncated: false, nodesTruncated: false, commandInvalidSamples: 0 }
  ] });
  assert.equal(aggregate.frameCount, 2);
  assert.deepEqual(aggregate.command.map((item) => item.name), ["Text", "FillRect", "Image", "BoxShadow"]);
  assert.equal(aggregate.command.find((item) => item.name === "Text").count, 5);
  assert.equal(aggregate.command.find((item) => item.name === "Text").totalUs, 1300);
  assert.equal(aggregate.command.find((item) => item.name === "Text").p95Us, 500);
  assert.equal(aggregate.stage.find((item) => item.name === "paint").count, 2);
  assert.equal(aggregate.stage.find((item) => item.name === "paint").totalUs, 2200);
  assert.equal(aggregate.owner.find((item) => item.name === "unattributed").count, 4);
  assert.equal(aggregate.missingAttribution.totalUs, 900);
  assert.equal(aggregate.invalidCommandSamples, 1);
  assert.equal(aggregate.commandsTruncatedFrames, 1);
  const html = renderTraceHtml(parsed, true, "trace.jsonl", {
    cspSource: "vscode-resource:",
    frameImages: { "0": "vscode-resource://frame_000.bmp" }
  });
  assert(html.includes("frameSlider"));
  assert(html.includes("slowestFrameButton"));
  assert(html.includes("allUnattributed"));
  assert(html.includes("partialAttribution"));
  assert(html.includes("unattributed"));
  assert(html.includes("id:title"));
  assert(html.includes("legacy-card"));
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
  assert(html.includes("跨帧聚合"));
  assert(html.includes("aggregateView"));
  assert(html.includes("missingAttribution"));
  assert(html.includes("p95Us"));
  assert(html.includes("owner=typeof item.owner"));
  assert(!html.includes("ignored</code>"));
  assert(!html.includes("invalid,0 10x10"));
  assert(html.includes("img-src vscode-resource:"));

  const commandsPosition = html.indexOf("const commands=");
  const sortPosition = html.indexOf("sort((left,right)=>right.us-left.us)", commandsPosition);
  const slicePosition = html.indexOf("slice(0,64)", sortPosition);
  assert(sortPosition >= 0 && sortPosition < slicePosition, "commands are sorted before top-N truncation");

  const invalid = parseRenderTrace(`${JSON.stringify(session)}\n${JSON.stringify({ ...frame, frame: 2 })}\n${JSON.stringify({ ...frame, frame: 1 })}\n`);
  assert.equal(invalid.frames.length, 1);
  assert(invalid.errors.some((error) => error.includes("strictly increasing")));
}

main();
