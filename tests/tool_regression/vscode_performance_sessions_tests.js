const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const {
  HISTORY_LIMIT,
  SESSION_FORMAT,
  artifactKind,
  createPerformanceSession,
  defaultArtifactPaths,
  discoverPerformanceArtifacts,
  finalizePerformanceSession,
  historyFiles,
  performanceInputsFromPaths,
  readPerformanceHistory
} = require("../../tools/vscode-jellyframe/performance_sessions");

function write(filePath, contents) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, contents, "utf8");
  return filePath;
}

function main() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "jellyframe-performance-sessions-"));
  try {
    const app = path.join(root, "app");
    const build = path.join(app, ".jellyframe", "build");
    const outside = path.join(root, "operator evidence", "device-profile.json");
    fs.mkdirSync(app, { recursive: true });
    const report = write(path.join(build, "vscode-app-check-report.json"), JSON.stringify({
      format: "jellyframe.package.report"
    }));
    const trace = write(path.join(build, "debug", "render-trace.jsonl"),
      `${JSON.stringify({ format: "jellyframe.render.trace.v0", type: "session" })}\n`);
    write(path.join(build, "performance", "old", "inputs", "ignored.json"), JSON.stringify({
      format: "jellyframe.package.report"
    }));
    write(outside, JSON.stringify({ format: "jellyframe.device.profile.v0" }));
    const microbench = write(path.join(build, "probe-output.txt"), "opaque_fill iterations=100 avg_us=12.5\n");
    const serialLog = write(path.join(root, "serial.log"),
      "I profile: device_profile window=1 frame_us_p95=17000\n");
    const generated = write(path.join(build, "vscode-app-performance.json"), JSON.stringify({
      format: "jellyframe.render.performance.report"
    }));

    assert.equal(artifactKind(report), "packageReport");
    assert.equal(artifactKind(trace), "trace");
    assert.equal(artifactKind(outside), "deviceTelemetry");
    assert.equal(artifactKind(microbench), "microbench");
    assert.equal(artifactKind(serialLog), "deviceTelemetry");
    assert.equal(artifactKind(generated), undefined, "generated reports are not reused as raw inputs");
    const baseline = write(path.join(build, "baseline.txt"), "opaque_fill iterations=100 avg_us=14.0\n");
    assert.deepStrictEqual(performanceInputsFromPaths([microbench, baseline]), [
      { kind: "microbench", path: microbench },
      { kind: "microbenchBaseline", path: baseline }
    ]);

    const discovered = discoverPerformanceArtifacts({ buildRoot: build, lastTracePath: trace });
    assert.equal(discovered.filter((item) => item.kind === "trace").length, 1, "last trace is de-duplicated");
    assert(discovered.some((item) => item.path === fs.realpathSync(report)));
    assert(!discovered.some((item) => item.name === "ignored.json"), "past session snapshots are not rediscovered");
    const defaults = defaultArtifactPaths(discovered);
    assert.equal(new Set(defaults.map((file) => artifactKind(file))).size, defaults.length);

    const first = createPerformanceSession({
      buildRoot: build,
      appRoot: app,
      inputs: [
        { kind: "packageReport", path: report },
        { kind: "trace", path: trace },
        { kind: "deviceTelemetry", path: outside }
      ],
      sourceCommit: "0123456789abcdef",
      runtimeIdentity: { runtimeVersion: "0.6.0-dev", renderCoreVersion: "0.6.2", renderCoreAbi: 1 },
      now: new Date("2026-09-17T12:34:56.000Z")
    });
    assert.equal(first.manifest.format, SESSION_FORMAT);
    assert.equal(first.manifest.inputs.length, 3);
    assert(!JSON.stringify(first.manifest).includes(path.dirname(outside)), "external source paths are not persisted");
    assert(Object.values(first.inputPaths).every((file) => file.startsWith(first.sessionDirectory)));
    write(first.output, JSON.stringify({ format: "jellyframe.render.performance.report" }));
    write(first.htmlOutput, "<!doctype html>");
    const completed = finalizePerformanceSession(first, { success: true });
    assert.equal(completed.status, "complete");
    assert(historyFiles(build, completed).html.endsWith("report.html"));

    for (let index = 0; index < HISTORY_LIMIT + 3; ++index) {
      const session = createPerformanceSession({
        buildRoot: build,
        appRoot: app,
        inputs: [{ kind: "packageReport", path: report }],
        now: new Date(Date.UTC(2026, 8, 18, 0, 0, index))
      });
      finalizePerformanceSession(session, { success: false, error: `failure ${index}` });
    }
    const history = readPerformanceHistory(build);
    assert.equal(history.sessions.length, HISTORY_LIMIT, "history remains bounded");
    assert.equal(history.sessions[0].status, "failed");

    write(path.join(build, "performance", "history.json"), "{ malformed");
    assert.deepStrictEqual(readPerformanceHistory(build).sessions, [], "malformed history fails closed");
    assert.equal(historyFiles(build, { manifest: "../../outside.json" }).manifest, undefined, "history paths cannot escape");
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
}

main();
