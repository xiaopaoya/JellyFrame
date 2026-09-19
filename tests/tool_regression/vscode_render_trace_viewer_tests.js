const assert = require("assert");
const fs = require("fs");
const { parseRenderTrace, aggregateTrace, frameTimingBreakdown, frameStageComposition, frameStageTimeline, frameCommandTimeline, frameDirtyRepaintEvidence, frameMutationEvidence, frameHotspotSummary, frameDeltaSummary, frameAnomalySummary, frameAnomalyAttribution, frameTimingSummary, renderTraceHtml } = require("../../tools/vscode-jellyframe/render_trace_viewer");
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

const { parseEmbeddedTraceLine, traceDirectoryNames, traceFrameImageName } = loadTraceHelpers();

function main() {
  assert.equal(
    JSON.stringify(parseEmbeddedTraceLine("JF_TRACE_STARTED\tC:\\trace path\\live.jsonl")),
    JSON.stringify({ type: "started", path: "C:\\trace path\\live.jsonl" })
  );
  assert.equal(
    JSON.stringify(parseEmbeddedTraceLine("JF_TRACE\tC:\\trace path\\live.jsonl\t17\t3")),
    JSON.stringify({ type: "complete", path: "C:\\trace path\\live.jsonl", frames: 17, dropped: 3 })
  );
  assert.equal(
    JSON.stringify(parseEmbeddedTraceLine("JF_TRACE_ERROR cannot publish trace")),
    JSON.stringify({ type: "error", error: "cannot publish trace" })
  );
  assert.equal(parseEmbeddedTraceLine("JF_TRACE\ttrace.jsonl\tinvalid\t0"), undefined);
  assert.equal(parseEmbeddedTraceLine("ordinary runtime output"), undefined);

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
    mutationSources: [
      { kind: "input", owner: "id:title", dirtyFlags: 32, mutationGeneration: 4, count: 1, mutation: true, invalidation: true, dirtyRectIndexes: [0], ownerBounds: { x: 8, y: 12, width: 64, height: 28 } }
    ],
    mutationSourcesTruncated: true,
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
  assert.deepEqual(frameTimingBreakdown(frame), {
    totalUs: 2000,
    recordedStageUs: 1500,
    unaccountedUs: 500,
    timingComplete: false
  });
  assert.deepEqual(frameTimingBreakdown({ totalUs: 10, stagesUs: { layout: 20 } }), {
    totalUs: 10,
    recordedStageUs: 20,
    unaccountedUs: 0,
    timingComplete: false
  });
  assert.deepEqual(frameStageComposition(frame), {
    totalUs: 2000,
    recordedStageUs: 1500,
    unaccountedUs: 500,
    timingComplete: false,
    denominatorUs: 2000,
    overrunUs: 0,
    segments: [
      { name: "layout", us: 500, kind: "stage", widthPercent: 25, frameSharePercent: 25 },
      { name: "paint", us: 1000, kind: "stage", widthPercent: 50, frameSharePercent: 50 },
      { name: "unaccounted", us: 500, kind: "unaccounted", widthPercent: 25, frameSharePercent: 25 }
    ]
  });
  assert.deepEqual(frameStageComposition({ totalUs: 10, stagesUs: { layout: 20 } }), {
    totalUs: 10,
    recordedStageUs: 20,
    unaccountedUs: 0,
    timingComplete: false,
    denominatorUs: 20,
    overrunUs: 10,
    segments: [
      { name: "layout", us: 20, kind: "stage", widthPercent: 100, frameSharePercent: 200 }
    ]
  });
  const spanFrame = {
    ...frame,
    frame: 1,
    stageSpans: [
      { name: "input", startUs: 0, durationUs: 100 },
      { name: "paint", startUs: 140, durationUs: 300 }
    ]
  };
  assert.deepEqual(frameStageTimeline(spanFrame), {
    available: true,
    totalUs: 2000,
    denominatorUs: 2000,
    overrunUs: 0,
    segments: [
      { name: "input", kind: "stage", startUs: 0, durationUs: 100, leftPercent: 0, widthPercent: 5 },
      { name: "unaccounted", kind: "gap", startUs: 100, durationUs: 40, leftPercent: 5, widthPercent: 2 },
      { name: "paint", kind: "stage", startUs: 140, durationUs: 300, leftPercent: 7, widthPercent: 15 },
      { name: "unaccounted", kind: "gap", startUs: 440, durationUs: 1560, leftPercent: 22, widthPercent: 78 }
    ]
  });
  const commandFrame = {
    ...spanFrame,
    commandSpans: [
      { type: "FillRect", owner: "id:card", startUs: 3200, durationUs: 70, pixels: 100, rect: { x: 8, y: 12, width: 64, height: 28 } },
      { type: "Text", owner: "id:title", startUs: 3310, durationUs: 90, pixels: 24 }
    ]
  };
  assert.deepEqual(frameCommandTimeline(commandFrame), {
    available: true,
    totalUs: 2000,
    denominatorUs: 3400,
    overrunUs: 1400,
    segments: [
      { name: "unaccounted", kind: "gap", startUs: 0, durationUs: 3200, leftPercent: 0, widthPercent: 3200 * 100 / 3400 },
      { name: "FillRect · id:card", type: "FillRect", owner: "id:card", pixels: 100, kind: "command", startUs: 3200, durationUs: 70, leftPercent: 3200 * 100 / 3400, widthPercent: 70 * 100 / 3400 },
      { name: "unaccounted", kind: "gap", startUs: 3270, durationUs: 40, leftPercent: 3270 * 100 / 3400, widthPercent: 40 * 100 / 3400 },
      { name: "Text · id:title", type: "Text", owner: "id:title", pixels: 24, kind: "command", startUs: 3310, durationUs: 90, leftPercent: 3310 * 100 / 3400, widthPercent: 90 * 100 / 3400 }
    ]
  });
  assert.deepEqual(frameDirtyRepaintEvidence(commandFrame), {
    available: true,
    entries: [{
      name: "FillRect · id:card",
      type: "FillRect",
      owner: "id:card",
      durationUs: 70,
      pixels: 100,
      dirtyRectIndexes: [0],
      overlapPixels: 1792
    }]
  });
  const mutationEvidence = frameMutationEvidence({
    ...commandFrame,
    mutationSources: [
      { kind: "script", owner: "id:title", dirtyFlags: 4, mutationGeneration: 5, count: 1, mutation: true, invalidation: true, dirtyRectIndexes: [0] },
      { kind: "input", owner: "id:card", dirtyFlags: 32, mutationGeneration: 5, count: 1, mutation: false, invalidation: true, dirtyRectIndexes: [0] }
    ]
  });
  assert.equal(mutationEvidence.available, true);
  assert.deepEqual(mutationEvidence.entries, [
    { kind: "script", owner: "id:title", commandUs: 90, commandCount: 1, dirtyEvidenceUs: 0, dirtyEvidenceHits: 0, evidence: "owner-command" },
    { kind: "input", owner: "id:card", commandUs: 70, commandCount: 1, dirtyEvidenceUs: 70, dirtyEvidenceHits: 1, evidence: "owner-command" }
  ]);
  const linkedCommandFrame = {
    ...spanFrame,
    frame: 2,
    stagesUs: {},
    commands: [],
    commandInvalidSamples: 0,
    commandsTruncated: false,
    nodesTruncated: false,
    commandSpans: [
      { type: "Text", owner: "id:title", startUs: 150, durationUs: 20, pixels: 24 }
    ]
  };
  const linkedTimeline = frameCommandTimeline(linkedCommandFrame);
  const linkedSegment = linkedTimeline.segments.find((segment) => segment.kind === "command");
  assert.equal(linkedSegment.stageName, "paint");
  assert.equal(linkedSegment.stageOverlapUs, 20);
  assert.deepEqual(frameHotspotSummary(frame), {
    stage: { name: "paint", us: 1000 },
    command: { name: "Text", us: 1000, pixels: 20, samples: 2 },
    owner: { name: "id:title", us: 1000, samples: 2 }
  });
  const delta = frameDeltaSummary(frame, {
    frame: 99,
    totalUs: 1500,
    stagesUs: { layout: 600, paint: 800 },
    dirtyRectCount: 2,
    dirtyAreaPercent: 1,
    commands: [{ type: "Text", owner: "id:title", us: 800, pixels: 20, samples: 1 }]
  });
  assert.equal(delta.available, true);
  assert.equal(delta.totalDeltaUs, 500);
  assert.equal(delta.dirtyRectCountDelta, -1);
  assert.equal(delta.dirtyAreaPercentDelta, 2);
  assert.deepEqual(delta.stages, [
    { name: "paint", deltaUs: 200 },
    { name: "layout", deltaUs: -100 }
  ]);
  assert.equal(delta.commandComparable, true);
  assert.equal(delta.commands.find((item) => item.name === "Text · id:title").deltaUs, 200);
  assert.equal(delta.commands.find((item) => item.name === "FillRect · n2").deltaUs, 400);
  const anomalyFrames = Array.from({ length: 20 }, (_, index) => ({
    frame: index,
    totalUs: index === 19 ? 1000 : 100,
    stagesUs: { paint: index === 19 ? 80 : 20 },
    dirtyAreaPercent: index === 19 ? 50 : 1
  }));
  const anomalies = frameAnomalySummary({ frames: anomalyFrames });
  assert.deepEqual(anomalies.anomalies, [19]);
  assert.deepEqual(anomalies.frames[19].reasons, ["total", "paint", "dirty"]);
  const normalAttribution = frameAnomalyAttribution(
    { frame: 0, totalUs: 100, stagesUs: { paint: 20 }, dirtyAreaPercent: 1 },
    null,
    { totalUsP95: 100, paintUsP95: 20, dirtyAreaPercentP95: 1 }
  );
  assert.equal(normalAttribution.anomalous, false);
  assert.deepEqual(normalAttribution.reasons, []);
  const anomalousAttribution = frameAnomalyAttribution({
    frame: 20,
    totalUs: 1000,
    timingComplete: true,
    stagesUs: { layout: 100, paint: 80 },
    dirtyAreaPercent: 50,
    dirtyRects: [{ x: 0, y: 0, width: 20, height: 20 }],
    commandSpans: [
      { type: "FillRect", owner: "id:card", startUs: 100, durationUs: 70, pixels: 400, rect: { x: 0, y: 0, width: 20, height: 20 } },
      { type: "Text", owner: "id:title", startUs: 180, durationUs: 90, pixels: 40 }
    ],
    commandSpansTruncated: false,
    dirtyRectsTruncated: false
  }, {
    frame: 19,
    totalUs: 900,
    stagesUs: { layout: 100, paint: 60 },
    dirtyAreaPercent: 40,
    commandSpans: [
      { type: "FillRect", owner: "id:card", startUs: 100, durationUs: 60, pixels: 400, rect: { x: 0, y: 0, width: 20, height: 20 } },
      { type: "Text", owner: "id:title", startUs: 180, durationUs: 10, pixels: 40 }
    ]
  }, { totalUsP95: 800, paintUsP95: 70, dirtyAreaPercentP95: 45 });
  assert.deepEqual(anomalousAttribution.reasons, ["total", "paint", "dirty"]);
  assert.deepEqual(anomalousAttribution.hottestStage, { name: "layout", us: 100 });
  assert.equal(anomalousAttribution.hottestCommand.name, "Text · id:title");
  assert.deepEqual(anomalousAttribution.dirtyOwner, {
    name: "id:card", us: 70, overlapPixels: 400, hits: 1
  });
  assert.equal(anomalousAttribution.largestCommandIncrease.name, "Text · id:title");
  assert.equal(anomalousAttribution.largestCommandIncrease.deltaUs, 80);
  assert.deepEqual(anomalousAttribution.limitations, []);
  const limitedAttribution = frameAnomalyAttribution({
    frame: 21, totalUs: 1000, stagesUs: { paint: 80 }, dirtyAreaPercent: 50,
    commandSpans: [], commandSpansTruncated: true, dirtyRectsTruncated: true,
    timingComplete: false
  }, null, { totalUsP95: 800, paintUsP95: 70, dirtyAreaPercentP95: 45 });
  assert.equal(limitedAttribution.anomalous, true);
  assert.deepEqual(limitedAttribution.limitations, [
    "no-command-timing", "command-data-truncated", "dirty-evidence-unavailable",
    "no-previous-frame", "partial-frame-timing"
  ]);
  assert.deepEqual(frameTimingSummary({ frames: [frame, { ...frame, totalUs: 3000 }] }), {
    count: 2,
    p50Us: 2000,
    p95Us: 3000,
    maxUs: 3000
  });
  const aggregate = aggregateTrace({ frames: [
    { ...frame, commandSpans: [] },
    linkedCommandFrame,
    { ...frame, frame: 1, totalUs: 3000, stagesUs: { layout: 700, paint: 1200 }, commands: [
      { type: "Text", owner: "id:title", us: 300, pixels: 20, samples: 3 },
      { type: "FillRect", us: 600, pixels: 50, samples: 2 },
      { type: "Image", us: 100, pixels: 4 }
    ], commandsTruncated: false, nodesTruncated: false, commandInvalidSamples: 0 }
  ] });
  assert.equal(aggregate.frameCount, 3);
  assert.deepEqual(aggregate.command.map((item) => item.name), ["Text", "FillRect", "Image", "BoxShadow"]);
  assert.equal(aggregate.command.find((item) => item.name === "Text").count, 5);
  assert.equal(aggregate.command.find((item) => item.name === "Text").totalUs, 1300);
  assert.equal(aggregate.command.find((item) => item.name === "Text").p95Us, 500);
  assert.equal(aggregate.stage.find((item) => item.name === "paint").count, 2);
  assert.equal(aggregate.stage.find((item) => item.name === "paint").totalUs, 2200);
  assert.equal(aggregate.owner.find((item) => item.name === "unattributed").count, 4);
  assert.equal(aggregate.missingAttribution.totalUs, 900);
  assert.deepEqual(aggregate.commandSpan, [{
    name: "Text · id:title",
    type: "Text",
    owner: "id:title",
    stage: "paint",
    count: 1,
    totalUs: 20,
    p95Us: 20,
    totalPixels: 24
  }]);
  const dirtyAggregate = aggregateTrace({ frames: [commandFrame] });
  assert.deepEqual(dirtyAggregate.dirtyEvidence, [{
    name: "FillRect · id:card",
    type: "FillRect",
    owner: "id:card",
    count: 1,
    totalUs: 70,
    p95Us: 70,
    totalOverlapPixels: 1792,
    dirtyRectHits: 1
  }]);
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
  assert(html.includes("未归因时间"));
  assert(html.includes("单帧阶段构成"));
  assert(html.includes("stage-composition"));
  assert(html.includes("data-stage-index"));
  assert(html.includes("frameComposition"));
  assert(html.includes("trace producer（未声明 runtime）"));
  const spanHtml = renderTraceHtml(parseRenderTrace(`${JSON.stringify(session)}\n${JSON.stringify(spanFrame)}\n`), true, "span-trace.jsonl");
  assert(spanHtml.includes("单帧实际时间线"));
  assert(spanHtml.includes("stage-timeline"));
  assert(spanHtml.includes("data-timeline-index"));
  assert(spanHtml.includes("stageSpans"));
  const commandHtml = renderTraceHtml(parseRenderTrace(`${JSON.stringify(session)}\n${JSON.stringify(commandFrame)}\n`), true, "command-trace.jsonl");
  assert(commandHtml.includes("绘制命令实际时间线"));
  assert(commandHtml.includes("data-command-timeline-index"));
  assert(commandHtml.includes("commandSpans"));
  assert(html.includes("当前帧热点"));
  assert(html.includes("跨帧总耗时"));
  assert(html.includes("frameSummaryView"));
  assert(html.includes("vscode-resource://frame_000.bmp"));
  assert(html.includes("dirtyCoverage"));
  assert(html.includes("dirtyRects"));
  assert(html.includes("frameAnomalyAttributions"));
  assert(html.includes("anomalyAttribution"));
  assert(html.includes("脏区内的实际重绘证据"));
  assert(html.includes("frameDirtyEvidence"));
  assert(html.includes("mutationSources"));
  assert(html.includes("输入 / mutation 来源"));
  assert(html.includes("sourceCommandTime"));
  assert(html.includes("相邻帧变化"));
  assert(html.includes("frameDeltas"));
  assert(html.includes("仅显示异常帧"));
  assert(html.includes("帧耗时趋势"));
  assert(html.includes("data-trend-index"));
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
  assert(html.includes("实际命令 · 元素"));
  assert(html.includes("commandSpan"));
  assert(html.includes("commandSpansTruncatedFrames"));
  assert(html.includes("脏区重绘证据 · 元素"));
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
  const invalidSpans = parseRenderTrace(`${JSON.stringify(session)}\n${JSON.stringify({ ...frame, stageSpans: [{ name: "paint", startUs: -1, durationUs: 1 }] })}\n`);
  assert(invalidSpans.errors.some((error) => error.includes("stage spans")));
  const invalidCommandSpans = parseRenderTrace(`${JSON.stringify(session)}\n${JSON.stringify({ ...frame, commandSpans: [{ type: "Text", owner: "id:x", startUs: 0, durationUs: 1, pixels: -1 }] })}\n`);
  assert(invalidCommandSpans.errors.some((error) => error.includes("command spans")));
  const overflowSpan = parseRenderTrace(`${JSON.stringify(session)}\n${JSON.stringify({ ...frame, commandSpans: [{ type: "Text", owner: "id:x", startUs: Number.MAX_SAFE_INTEGER, durationUs: 1, pixels: 1 }] })}\n`);
  assert(overflowSpan.errors.some((error) => error.includes("command spans")));
  const invalidRect = parseRenderTrace(`${JSON.stringify(session)}\n${JSON.stringify({ ...frame, commandSpans: [{ type: "Text", owner: "id:x", startUs: 0, durationUs: 1, pixels: 1, rect: { x: 0, y: 0, width: -1, height: 1 } }] })}\n`);
  assert(invalidRect.errors.some((error) => error.includes("command spans")));
  const invalidMutationSource = parseRenderTrace(`${JSON.stringify(session)}\n${JSON.stringify({ ...frame, mutationSources: [{ kind: "unknown", owner: "unattributed", dirtyFlags: 32, mutationGeneration: 1, count: 1, mutation: true, invalidation: true, dirtyRectIndexes: [] }] })}\n`);
  assert(invalidMutationSource.errors.some((error) => error.includes("mutation sources")));
}

main();
