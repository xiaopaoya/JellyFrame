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
  comparePerformanceProfiles,
  performanceProfile,
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

    const baselineReport = {
      format: "jellyframe.render.performance.report",
      summary: { frameCount: 0 },
      deviceTelemetry: [{
        identity: { case: "embedded-ui-scroll", profile: "ws147-v0", board: "ws147", viewport: "172x320" },
        metrics: { frameP95Us: 10000, paintP95Us: 6000, presentP95Us: 3000 }
      }]
    };
    const candidateReport = JSON.parse(JSON.stringify(baselineReport));
    candidateReport.deviceTelemetry[0].metrics.frameP95Us = 10500;
    assert.equal(comparePerformanceProfiles(
      performanceProfile(candidateReport), performanceProfile(baselineReport)
    ).status, "regressed");

    const baselineSession = createPerformanceSession({
      buildRoot: build,
      appRoot: app,
      inputs: [{ kind: "deviceTelemetry", path: outside }],
      sourceCommit: "baseline-commit",
      runtimeIdentity: { renderCoreVersion: "0.6.2", renderCoreAbi: 1 },
      now: new Date("2026-09-17T13:00:00.000Z")
    });
    write(baselineSession.output, JSON.stringify(baselineReport));
    write(baselineSession.htmlOutput, "<!doctype html>");
    const baselineEntry = finalizePerformanceSession(baselineSession, { success: true });
    assert.equal(baselineEntry.comparison, undefined);

    const candidateSession = createPerformanceSession({
      buildRoot: build,
      appRoot: app,
      inputs: [{ kind: "deviceTelemetry", path: outside }],
      sourceCommit: "candidate-commit",
      runtimeIdentity: { renderCoreVersion: "0.6.2", renderCoreAbi: 1 },
      now: new Date("2026-09-17T14:00:00.000Z")
    });
    write(candidateSession.output, JSON.stringify(candidateReport));
    write(candidateSession.htmlOutput, "<!doctype html>");
    const candidateEntry = finalizePerformanceSession(candidateSession, { success: true });
    assert.equal(candidateEntry.comparison.status, "regressed");
    assert.equal(candidateEntry.comparison.baselineSessionId, baselineEntry.id);
    assert.equal(candidateEntry.comparison.metrics[0].deltaPercent, 5);
    assert.equal(JSON.parse(fs.readFileSync(candidateSession.manifestPath, "utf8")).comparison.status, "regressed");

    const runtimeCandidate = createPerformanceSession({
      buildRoot: build,
      appRoot: app,
      inputs: [{ kind: "deviceTelemetry", path: outside }],
      sourceCommit: "candidate-commit",
      runtimeIdentity: { runtimeVersion: "0.6.0-dev", renderCoreVersion: "0.6.3", renderCoreAbi: 1 },
      now: new Date("2026-09-17T15:00:00.000Z")
    });
    write(runtimeCandidate.output, JSON.stringify(baselineReport));
    write(runtimeCandidate.htmlOutput, "<!doctype html>");
    const runtimeEntry = finalizePerformanceSession(runtimeCandidate, { success: true });
    assert(runtimeEntry.comparison, "a Runtime/Core version change is comparable even when the App commit is unchanged");

    const incompatibleAbi = createPerformanceSession({
      buildRoot: build,
      appRoot: app,
      inputs: [{ kind: "deviceTelemetry", path: outside }],
      sourceCommit: "abi-break",
      runtimeIdentity: { renderCoreAbi: 2 },
      now: new Date("2026-09-17T16:00:00.000Z")
    });
    write(incompatibleAbi.output, JSON.stringify(candidateReport));
    write(incompatibleAbi.htmlOutput, "<!doctype html>");
    assert.equal(finalizePerformanceSession(incompatibleAbi, { success: true }).comparison, undefined,
      "different Render Core ABIs are never compared");

    const mismatched = JSON.parse(JSON.stringify(candidateReport));
    mismatched.deviceTelemetry[0].identity.case = "embedded-ui-static";
    assert.equal(comparePerformanceProfiles(
      performanceProfile(mismatched), performanceProfile(baselineReport)
    ), undefined, "different workloads are never compared");

    const desktopBaseline = {
      format: "jellyframe.render.performance.report",
      metadata: { appId: "org.example.app", profile: "scroll", viewport: { width: 172, height: 320 } },
      summary: { frameCount: 120, totalUs: { p95: 20000 } },
      deviceTelemetry: []
    };
    const desktopCandidate = JSON.parse(JSON.stringify(desktopBaseline));
    desktopCandidate.summary.totalUs.p95 = 18000;
    assert.equal(comparePerformanceProfiles(
      performanceProfile(desktopCandidate), performanceProfile(desktopBaseline)
    ).status, "improved");
    delete desktopCandidate.metadata.appId;
    assert.equal(performanceProfile(desktopCandidate).sources.length, 0,
      "desktop traces without complete workload identity are not compared");

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
