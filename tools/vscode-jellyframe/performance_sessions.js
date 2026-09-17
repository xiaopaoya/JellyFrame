const crypto = require("crypto");
const fs = require("fs");
const path = require("path");

const SESSION_FORMAT = "jellyframe.performance.session";
const HISTORY_FORMAT = "jellyframe.performance.history";
const TREND_FORMAT = "jellyframe.performance.device-trend";
const HISTORY_LIMIT = 20;
const MAX_INPUT_BYTES = 64 * 1024 * 1024;
const MAX_SESSION_INPUT_BYTES = 128 * 1024 * 1024;
const REGRESSION_PERCENT = 3;
const IMPROVEMENT_PERCENT = -5;
const MAX_COMPARISON_METRICS = 32;
const INPUT_KINDS = new Set([
  "packageReport",
  "trace",
  "deviceTelemetry",
  "microbench",
  "microbenchBaseline"
]);

function isInside(child, parent) {
  const relative = path.relative(path.resolve(parent), path.resolve(child));
  return relative === "" || (relative !== ".." && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative));
}

function safeName(value, fallback = "artifact") {
  const result = String(value || "").replace(/[^a-zA-Z0-9_.-]/g, "_").replace(/^\.+/, "");
  return result || fallback;
}

function appKeyForRoot(appRoot) {
  return crypto.createHash("sha256").update(path.resolve(appRoot)).digest("hex").slice(0, 12);
}

function readPrefix(filePath, limit = 64 * 1024) {
  const descriptor = fs.openSync(filePath, "r");
  try {
    const buffer = Buffer.alloc(limit);
    const bytes = fs.readSync(descriptor, buffer, 0, buffer.length, 0);
    return buffer.subarray(0, bytes).toString("utf8");
  } finally {
    fs.closeSync(descriptor);
  }
}

function artifactKind(filePath) {
  let stat;
  try {
    stat = fs.statSync(filePath);
  } catch (_) {
    return undefined;
  }
  if (!stat.isFile()) {
    return undefined;
  }
  const name = path.basename(filePath).toLowerCase();
  const extension = path.extname(name);
  let prefix = "";
  try {
    prefix = readPrefix(filePath);
  } catch (_) {
    return undefined;
  }
  const firstLine = prefix.split(/\r?\n/, 1)[0].trim();
  let format;
  try {
    const value = JSON.parse(firstLine || prefix);
    format = typeof value?.format === "string" ? value.format : undefined;
  } catch (_) {
    const match = /"format"\s*:\s*"([^"]+)"/.exec(prefix);
    format = match?.[1];
  }
  if (format === "jellyframe.render.performance.report") {
    return undefined;
  }
  if (format === "jellyframe.package.report") {
    return "packageReport";
  }
  if (format === "jellyframe.render.trace.v0") {
    return "trace";
  }
  if (format === "jellyframe.device.profile.v0" || format === "jellyframe.port.telemetry.metrics.v0") {
    return "deviceTelemetry";
  }
  if (/\bdevice_profile(?:_timing|_pipeline|_present|_counters)?\s+/.test(prefix)
      || /\b(frame_us_p95|present_us_p95|dma_wait_us_p95|dirty_pixels_avg)=/.test(prefix)) {
    return "deviceTelemetry";
  }
  if (/microbench/i.test(format || "") || /microbench/.test(name)) {
    return "microbench";
  }
  if (/^[a-zA-Z0-9_]+\s+(?:iterations=\d+\s+avg_us=|samples=\d+\s+iterations_per_sample=)/m.test(prefix)) {
    return "microbench";
  }
  if ((extension === ".jsonl" || extension === ".trace") && /trace/.test(name)) {
    return "trace";
  }
  if ([".json", ".log", ".txt"].includes(extension) && /(device.*(profile|telemetry)|(profile|telemetry).*device)/.test(name)) {
    return "deviceTelemetry";
  }
  return undefined;
}

function discoverPerformanceArtifacts({ buildRoot, lastTracePath, maxFiles = 1000 } = {}) {
  const results = [];
  const seen = new Set();
  const add = (filePath) => {
    let resolved;
    try {
      resolved = fs.realpathSync(filePath);
    } catch (_) {
      return;
    }
    const key = process.platform === "win32" ? resolved.toLowerCase() : resolved;
    if (seen.has(key)) {
      return;
    }
    const kind = artifactKind(resolved);
    if (!kind) {
      return;
    }
    const stat = fs.statSync(resolved);
    seen.add(key);
    results.push({ kind, path: resolved, name: path.basename(resolved), mtimeMs: stat.mtimeMs, bytes: stat.size });
  };
  if (lastTracePath) {
    add(lastTracePath);
  }
  const pending = buildRoot ? [{ directory: path.resolve(buildRoot), depth: 0 }] : [];
  let visited = 0;
  while (pending.length && visited < maxFiles) {
    const current = pending.shift();
    let entries;
    try {
      entries = fs.readdirSync(current.directory, { withFileTypes: true });
    } catch (_) {
      continue;
    }
    for (const entry of entries) {
      if (visited++ >= maxFiles) break;
      const filePath = path.join(current.directory, entry.name);
      if (entry.isDirectory()) {
        if (current.depth < 2 && entry.name !== "performance") {
          pending.push({ directory: filePath, depth: current.depth + 1 });
        }
      } else if (entry.isFile()) {
        add(filePath);
      }
    }
  }
  return results.sort((left, right) => right.mtimeMs - left.mtimeMs || left.name.localeCompare(right.name));
}

function defaultArtifactPaths(artifacts) {
  const selected = [];
  const kinds = new Set();
  for (const artifact of artifacts || []) {
    if (!kinds.has(artifact.kind)) {
      selected.push(artifact.path);
      kinds.add(artifact.kind);
    }
  }
  return selected;
}

function performanceInputsFromPaths(paths) {
  const inputs = [];
  const assigned = new Set();
  for (const filePath of paths || []) {
    let kind = artifactKind(filePath);
    if (!kind) continue;
    if (kind === "microbench" && assigned.has(kind) && !assigned.has("microbenchBaseline")) {
      kind = "microbenchBaseline";
    }
    if (assigned.has(kind)) continue;
    assigned.add(kind);
    inputs.push({ kind, path: filePath });
  }
  return inputs;
}

function sha256(filePath) {
  const hash = crypto.createHash("sha256");
  hash.update(fs.readFileSync(filePath));
  return hash.digest("hex");
}

function timestampId(date = new Date()) {
  return date.toISOString().replace(/[-:]/g, "").replace(/\.\d{3}Z$/, "Z");
}

function writeJson(filePath, value) {
  fs.writeFileSync(filePath, `${JSON.stringify(value, null, 2)}\n`, "utf8");
}

function finiteMetric(value) {
  return typeof value === "number" && Number.isFinite(value) && value >= 0 ? value : undefined;
}

function sourceKey(parts) {
  return parts.map((value) => String(value || "").trim()).join("|");
}

function performanceProfile(report) {
  if (report?.format !== "jellyframe.render.performance.report") return { sources: [] };
  const sources = [];
  const frameCount = finiteMetric(report.summary?.frameCount);
  const desktopAppId = String(report.metadata?.appId || "").trim();
  const desktopProfile = String(report.metadata?.profile || "").trim();
  const viewport = report.metadata?.viewport;
  const viewportKey = viewport && typeof viewport === "object" && viewport.width && viewport.height
    ? `${viewport.width}x${viewport.height}`
    : "";
  if (frameCount > 0 && desktopAppId && desktopProfile && viewportKey) {
    const p95 = finiteMetric(report.summary?.totalUs?.p95);
    if (p95 !== undefined) {
      sources.push({
        kind: "desktopTrace",
        key: sourceKey(["desktop", desktopAppId, desktopProfile, viewportKey]),
        label: desktopProfile,
        metrics: { totalP95Us: p95 }
      });
    }
  }
  for (const telemetry of report.deviceTelemetry || []) {
    const identity = telemetry?.identity || {};
    if (![identity.case, identity.profile, identity.board, identity.viewport]
      .every((value) => String(value || "").trim())) {
      continue;
    }
    const metrics = {};
    for (const field of ["frameP95Us", "paintP95Us", "presentP95Us", "dmaWaitP95Us"]) {
      const value = finiteMetric(telemetry.metrics?.[field]);
      if (value !== undefined) metrics[field] = value;
    }
    if (Object.keys(metrics).length) {
      sources.push({
        kind: "deviceTelemetry",
        key: sourceKey(["device", identity.case, identity.profile, identity.board, identity.viewport]),
        label: identity.case,
        identity: {
          case: String(identity.case),
          profile: String(identity.profile),
          board: String(identity.board),
          viewport: String(identity.viewport)
        },
        metrics
      });
    }
  }
  return { sources };
}

function comparePerformanceProfiles(current, baseline) {
  const previous = new Map((baseline?.sources || []).map((source) => [`${source.kind}:${source.key}`, source]));
  const metrics = [];
  for (const source of current?.sources || []) {
    const matched = previous.get(`${source.kind}:${source.key}`);
    if (!matched) continue;
    for (const [name, currentValue] of Object.entries(source.metrics || {})) {
      const baselineValue = finiteMetric(matched.metrics?.[name]);
      if (baselineValue === undefined || baselineValue === 0 || finiteMetric(currentValue) === undefined) continue;
      const deltaPercent = Math.round(((currentValue - baselineValue) * 10000) / baselineValue) / 100;
      metrics.push({
        source: source.kind,
        workload: source.label,
        metric: name,
        baseline: baselineValue,
        current: currentValue,
        deltaPercent,
        status: deltaPercent > REGRESSION_PERCENT
          ? "regressed"
          : (deltaPercent <= IMPROVEMENT_PERCENT ? "improved" : "stable")
      });
      if (metrics.length >= MAX_COMPARISON_METRICS) break;
    }
    if (metrics.length >= MAX_COMPARISON_METRICS) break;
  }
  if (!metrics.length) return undefined;
  const status = metrics.some((metric) => metric.status === "regressed")
    ? "regressed"
    : (metrics.some((metric) => metric.status === "improved") ? "improved" : "stable");
  return { status, metrics };
}

function readPerformanceProfile(reportPath) {
  try {
    return performanceProfile(JSON.parse(fs.readFileSync(reportPath, "utf8")));
  } catch (_) {
    return { sources: [] };
  }
}

function compatibleRuntime(current, baseline) {
  const currentAbi = current?.renderCoreAbi;
  const baselineAbi = baseline?.renderCoreAbi;
  return currentAbi !== undefined && baselineAbi !== undefined && String(currentAbi) === String(baselineAbi);
}

function versionIdentityChanged(current, baseline) {
  const pairs = [
    [current.source?.commit, baseline.source?.commit],
    [current.runtime?.runtimeVersion, baseline.runtime?.runtimeVersion],
    [current.runtime?.renderCoreVersion, baseline.runtime?.renderCoreVersion],
    [current.runtime?.sdkRelease, baseline.runtime?.sdkRelease]
  ];
  return pairs.some(([left, right]) => left && right && String(left) !== String(right));
}

function previousVersionComparison(session, history, profile) {
  if (!profile.sources.length) return undefined;
  for (const entry of history.sessions || []) {
    if (entry.status !== "complete" || entry.app?.key !== session.manifest.app?.key
        || !versionIdentityChanged(session.manifest, entry)
        || !compatibleRuntime(session.manifest.runtime, entry.runtime)) {
      continue;
    }
    let baselineProfile = entry.performanceProfile;
    if (!Array.isArray(baselineProfile?.sources)) {
      const reportPath = historyFiles(path.dirname(session.performanceRoot), entry).report;
      if (!reportPath) continue;
      baselineProfile = readPerformanceProfile(reportPath);
    }
    const compared = comparePerformanceProfiles(profile, baselineProfile);
    if (compared) {
      return {
        ...compared,
        baselineSessionId: entry.id,
        ...(entry.source?.commit ? { baselineCommit: entry.source.commit } : {}),
        baselineRuntime: entry.runtime || {},
        limitations: [
          "Only identical source type and workload identity are compared.",
          "This local history trend is not a cross-device or cross-library benchmark claim."
        ]
      };
    }
  }
  return undefined;
}

function createPerformanceSession({
  buildRoot,
  appRoot,
  inputs,
  sourceCommit,
  runtimeIdentity,
  now = new Date()
}) {
  if (!buildRoot || !appRoot) {
    throw new Error("buildRoot and appRoot are required");
  }
  const performanceRoot = path.resolve(buildRoot, "performance");
  fs.mkdirSync(performanceRoot, { recursive: true });
  const appName = safeName(path.basename(appRoot), "app");
  const appKey = appKeyForRoot(appRoot);
  const baseId = `${appName}-${timestampId(now)}`;
  let sessionId = baseId;
  let suffix = 1;
  while (fs.existsSync(path.join(performanceRoot, sessionId))) {
    sessionId = `${baseId}-${suffix++}`;
  }
  const sessionDirectory = path.join(performanceRoot, sessionId);
  const inputDirectory = path.join(sessionDirectory, "inputs");
  fs.mkdirSync(inputDirectory, { recursive: true });
  if (!isInside(sessionDirectory, performanceRoot)) {
    throw new Error("performance session escaped the configured build directory");
  }

  const snapshots = [];
  const inputPaths = {};
  const usedNames = new Set();
  let totalInputBytes = 0;
  try {
    for (const input of inputs || []) {
      if (!INPUT_KINDS.has(input.kind)) {
        throw new Error(`unsupported performance input kind: ${input.kind}`);
      }
      const source = fs.realpathSync(input.path);
      const sourceStat = fs.statSync(source);
      if (!sourceStat.isFile()) {
        throw new Error(`performance input is not a file: ${input.path}`);
      }
      totalInputBytes += sourceStat.size;
      if (sourceStat.size > MAX_INPUT_BYTES || totalInputBytes > MAX_SESSION_INPUT_BYTES) {
        throw new Error("performance session input size exceeds the bounded snapshot limit");
      }
      let filename = `${input.kind}-${safeName(path.basename(source))}`;
      let collision = 1;
      while (usedNames.has(filename)) {
        const extension = path.extname(filename);
        filename = `${path.basename(filename, extension)}-${collision++}${extension}`;
      }
      usedNames.add(filename);
      const target = path.join(inputDirectory, filename);
      fs.copyFileSync(source, target);
      const relativeFile = path.relative(sessionDirectory, target).replace(/\\/g, "/");
      const stat = fs.statSync(target);
      snapshots.push({
        kind: input.kind,
        file: relativeFile,
        originalName: path.basename(source),
        bytes: stat.size,
        sha256: sha256(target)
      });
      inputPaths[input.kind] = target;
    }
    if (!snapshots.length) {
      throw new Error("at least one performance input is required");
    }
  } catch (error) {
    fs.rmSync(sessionDirectory, { recursive: true, force: true });
    throw error;
  }

  const manifestPath = path.join(sessionDirectory, "session.json");
  const output = path.join(sessionDirectory, "report.json");
  const htmlOutput = path.join(sessionDirectory, "report.html");
  const manifest = {
    format: SESSION_FORMAT,
    formatVersion: 1,
    id: sessionId,
    createdAt: now.toISOString(),
    status: "running",
    app: { name: appName, key: appKey },
    source: sourceCommit ? { commit: sourceCommit } : {},
    runtime: runtimeIdentity || {},
    inputs: snapshots,
    outputs: { json: "report.json", html: "report.html" }
  };
  writeJson(manifestPath, manifest);
  return { performanceRoot, sessionDirectory, manifestPath, manifest, inputPaths, output, htmlOutput };
}

function readPerformanceHistory(buildRoot) {
  const performanceRoot = path.resolve(buildRoot, "performance");
  const historyPath = path.join(performanceRoot, "history.json");
  try {
    const value = JSON.parse(fs.readFileSync(historyPath, "utf8"));
    if (value?.format !== HISTORY_FORMAT || value.formatVersion !== 1 || !Array.isArray(value.sessions)) {
      return { format: HISTORY_FORMAT, formatVersion: 1, sessions: [] };
    }
    const sessions = value.sessions.filter((entry) => {
      if (!entry || typeof entry.id !== "string" || typeof entry.manifest !== "string") return false;
      const manifest = path.resolve(performanceRoot, entry.manifest);
      return isInside(manifest, performanceRoot) && fs.existsSync(manifest);
    });
    return { format: HISTORY_FORMAT, formatVersion: 1, sessions };
  } catch (_) {
    return { format: HISTORY_FORMAT, formatVersion: 1, sessions: [] };
  }
}

function finalizePerformanceSession(session, { success, error } = {}) {
  const finishedAt = new Date().toISOString();
  const history = readPerformanceHistory(path.dirname(session.performanceRoot));
  const profile = success && fs.existsSync(session.output)
    ? readPerformanceProfile(session.output)
    : { sources: [] };
  const comparison = success ? previousVersionComparison(session, history, profile) : undefined;
  const manifest = {
    ...session.manifest,
    status: success ? "complete" : "failed",
    finishedAt,
    ...(profile.sources.length ? { performanceProfile: profile } : {}),
    ...(comparison ? { comparison } : {}),
    ...(success ? {} : { error: String(error || "performance report generation failed").slice(0, 500) })
  };
  writeJson(session.manifestPath, manifest);
  session.manifest = manifest;

  const relative = (filePath) => path.relative(session.performanceRoot, filePath).replace(/\\/g, "/");
  const entry = {
    id: manifest.id,
    createdAt: manifest.createdAt,
    finishedAt,
    status: manifest.status,
    app: manifest.app,
    source: manifest.source,
    runtime: manifest.runtime,
    manifest: relative(session.manifestPath),
    ...(manifest.performanceProfile ? { performanceProfile: manifest.performanceProfile } : {}),
    ...(manifest.comparison ? { comparison: manifest.comparison } : {}),
    ...(success && fs.existsSync(session.output) ? { report: relative(session.output) } : {}),
    ...(success && fs.existsSync(session.htmlOutput) ? { html: relative(session.htmlOutput) } : {})
  };
  history.sessions = [entry, ...history.sessions.filter((item) => item.id !== entry.id)].slice(0, HISTORY_LIMIT);
  fs.mkdirSync(session.performanceRoot, { recursive: true });
  writeJson(path.join(session.performanceRoot, "history.json"), history);
  return entry;
}

function historyFiles(buildRoot, entry) {
  const performanceRoot = path.resolve(buildRoot, "performance");
  const resolveSafe = (relative) => {
    if (!relative) return undefined;
    const filePath = path.resolve(performanceRoot, relative);
    return isInside(filePath, performanceRoot) && fs.existsSync(filePath) ? filePath : undefined;
  };
  return {
    manifest: resolveSafe(entry?.manifest),
    report: resolveSafe(entry?.report),
    html: resolveSafe(entry?.html)
  };
}

function deviceIdentityForSource(source) {
  if (source?.identity && [source.identity.case, source.identity.profile, source.identity.board, source.identity.viewport]
    .every((value) => String(value || "").trim())) {
    return source.identity;
  }
  const parts = String(source?.key || "").split("|");
  return parts.length === 5 && parts[0] === "device"
    ? { case: parts[1], profile: parts[2], board: parts[3], viewport: parts[4] }
    : undefined;
}

function buildDevicePerformanceTrend(history, appKey, now = new Date(), buildRoot) {
  const groups = new Map();
  for (const entry of history?.sessions || []) {
    if (entry?.status !== "complete" || entry.app?.key !== appKey || entry.runtime?.renderCoreAbi === undefined) {
      continue;
    }
    let profile = entry.performanceProfile;
    if (!Array.isArray(profile?.sources) && buildRoot) {
      const reportPath = historyFiles(buildRoot, entry).report;
      profile = reportPath ? readPerformanceProfile(reportPath) : undefined;
    }
    for (const source of profile?.sources || []) {
      const identity = deviceIdentityForSource(source);
      if (source?.kind !== "deviceTelemetry" || !source.key || !identity) continue;
      const abi = String(entry.runtime.renderCoreAbi);
      const key = `${source.key}|abi=${abi}`;
      let group = groups.get(key);
      if (!group) {
        group = {
          key,
          workload: source.label,
          identity,
          renderCoreAbi: abi,
          points: []
        };
        groups.set(key, group);
      }
      const metrics = {};
      for (const name of ["frameP95Us", "paintP95Us", "presentP95Us", "dmaWaitP95Us"]) {
        const value = finiteMetric(source.metrics?.[name]);
        if (value !== undefined) metrics[name] = value;
      }
      if (!Object.keys(metrics).length) continue;
      group.points.push({
        sessionId: entry.id,
        createdAt: entry.createdAt,
        ...(entry.source?.commit ? { commit: entry.source.commit } : {}),
        runtimeVersion: entry.runtime?.runtimeVersion,
        renderCoreVersion: entry.runtime?.renderCoreVersion,
        sdkRelease: entry.runtime?.sdkRelease,
        metrics
      });
    }
  }
  const series = [...groups.values()]
    .map((group) => ({
      ...group,
      points: group.points.sort((left, right) => String(left.createdAt).localeCompare(String(right.createdAt)))
    }))
    .filter((group) => group.points.length >= 2)
    .sort((left, right) => left.key.localeCompare(right.key));
  const app = (history?.sessions || []).find((entry) => entry.app?.key === appKey)?.app || { key: appKey };
  return {
    format: TREND_FORMAT,
    formatVersion: 1,
    generatedAt: now.toISOString(),
    app,
    historyLimit: HISTORY_LIMIT,
    series,
    limitations: [
      "Each series has an identical device case, profile, board, viewport, and Render Core ABI.",
      "Missing metrics remain gaps and are never converted to zero.",
      "This bounded local history is not a cross-device or cross-library benchmark."
    ]
  };
}

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function metricSegments(points, metric, xAt, yAt) {
  const segments = [];
  let current = [];
  for (let index = 0; index < points.length; ++index) {
    const value = finiteMetric(points[index].metrics?.[metric]);
    if (value === undefined) {
      if (current.length) segments.push(current);
      current = [];
    } else {
      current.push(`${xAt(index)},${yAt(value)}`);
    }
  }
  if (current.length) segments.push(current);
  return segments;
}

function renderDevicePerformanceTrendHtml(trend, chinese = false) {
  const labels = chinese ? {
    title: "设备性能趋势",
    empty: "尚无至少包含两个可比会话的设备趋势。",
    bounded: `仅显示最近 ${trend.historyLimit || HISTORY_LIMIT} 次项目性能会话中的可比设备数据。`,
    identity: "工作负载身份",
    session: "会话",
    version: "版本",
    time: "时间",
    limitations: "限制",
    metrics: { frameP95Us: "Frame p95", paintP95Us: "Paint p95", presentP95Us: "Present p95", dmaWaitP95Us: "DMA wait p95" }
  } : {
    title: "Device Performance Trends",
    empty: "No device trend has at least two comparable sessions yet.",
    bounded: `Only comparable device data from the latest ${trend.historyLimit || HISTORY_LIMIT} project sessions is shown.`,
    identity: "Workload identity",
    session: "Session",
    version: "Version",
    time: "Time",
    limitations: "Limitations",
    metrics: { frameP95Us: "Frame p95", paintP95Us: "Paint p95", presentP95Us: "Present p95", dmaWaitP95Us: "DMA wait p95" }
  };
  const colors = { frameP95Us: "#2ea043", paintP95Us: "#2f81f7", presentP95Us: "#bf8700", dmaWaitP95Us: "#a371f7" };
  const metricNames = Object.keys(labels.metrics);
  const sections = (trend.series || []).map((series) => {
    const width = 820;
    const height = 270;
    const left = 62;
    const right = 18;
    const top = 18;
    const bottom = 52;
    const plotWidth = width - left - right;
    const plotHeight = height - top - bottom;
    const allValues = series.points.flatMap((point) => metricNames
      .map((name) => finiteMetric(point.metrics?.[name])).filter((value) => value !== undefined));
    const maximum = Math.max(1, ...allValues) * 1.1;
    const xAt = (index) => Math.round(left + (series.points.length === 1 ? 0 : index * plotWidth / (series.points.length - 1)));
    const yAt = (value) => Math.round(top + plotHeight - value * plotHeight / maximum);
    const grid = [0, 0.25, 0.5, 0.75, 1].map((ratio) => {
      const y = Math.round(top + plotHeight * (1 - ratio));
      return `<line x1="${left}" y1="${y}" x2="${width - right}" y2="${y}" class="grid"/><text x="${left - 8}" y="${y + 4}" text-anchor="end">${(maximum * ratio / 1000).toFixed(1)}ms</text>`;
    }).join("");
    const lines = metricNames.map((metric) => metricSegments(series.points, metric, xAt, yAt)
      .map((segment) => `<polyline points="${segment.join(" ")}" fill="none" stroke="${colors[metric]}" stroke-width="2" vector-effect="non-scaling-stroke"/>`).join("")).join("");
    const dots = series.points.map((point, index) => metricNames.map((metric) => {
      const value = finiteMetric(point.metrics?.[metric]);
      return value === undefined ? "" : `<circle cx="${xAt(index)}" cy="${yAt(value)}" r="3" fill="${colors[metric]}"><title>${escapeHtml(labels.metrics[metric])}: ${(value / 1000).toFixed(2)}ms</title></circle>`;
    }).join("")).join("");
    const labelStep = Math.max(1, Math.ceil(series.points.length / 6));
    const xLabels = series.points.map((point, index) => {
      if (index % labelStep !== 0 && index !== series.points.length - 1) return "";
      const version = String(point.commit || point.renderCoreVersion || point.runtimeVersion || index + 1).slice(0, 10);
      return `<text x="${xAt(index)}" y="${height - 20}" text-anchor="middle">${escapeHtml(version)}</text>`;
    }).join("");
    const legend = metricNames.map((metric) => `<span><i style="background:${colors[metric]}"></i>${escapeHtml(labels.metrics[metric])}</span>`).join("");
    const rows = series.points.map((point) => {
      const version = [point.commit && String(point.commit).slice(0, 12), point.renderCoreVersion && `Core ${point.renderCoreVersion}`, point.runtimeVersion && `Runtime ${point.runtimeVersion}`].filter(Boolean).join(" · ") || "-";
      return `<tr><td><code>${escapeHtml(point.sessionId)}</code></td><td>${escapeHtml(version)}</td><td>${escapeHtml(point.createdAt)}</td>${metricNames.map((metric) => `<td>${point.metrics?.[metric] === undefined ? "-" : `${escapeHtml((point.metrics[metric] / 1000).toFixed(2))} ms`}</td>`).join("")}</tr>`;
    }).join("");
    const identity = series.identity || {};
    return `<section><h2>${escapeHtml(series.workload)}</h2><p class="muted">${escapeHtml(labels.identity)}: ${escapeHtml(identity.profile)} · ${escapeHtml(identity.board)} · ${escapeHtml(identity.viewport)} · ABI ${escapeHtml(series.renderCoreAbi)}</p><div class="legend">${legend}</div><svg viewBox="0 0 ${width} ${height}" role="img" aria-label="${escapeHtml(series.workload)}">${grid}${lines}${dots}${xLabels}</svg><div class="table-wrap"><table><tr><th>${escapeHtml(labels.session)}</th><th>${escapeHtml(labels.version)}</th><th>${escapeHtml(labels.time)}</th>${metricNames.map((metric) => `<th>${escapeHtml(labels.metrics[metric])}</th>`).join("")}</tr>${rows}</table></div></section>`;
  }).join("");
  return `<!doctype html><html><head><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'"><meta name="viewport" content="width=device-width,initial-scale=1"><title>${escapeHtml(labels.title)}</title><style>body{font-family:var(--vscode-font-family,system-ui);color:var(--vscode-foreground,#24292f);background:var(--vscode-editor-background,#fff);padding:20px;line-height:1.45}h1{font-size:22px}h2{font-size:17px;margin:0 0 4px}section{border-top:1px solid var(--vscode-panel-border,#d0d7de);padding:18px 0}.muted{color:var(--vscode-descriptionForeground,#57606a)}.legend{display:flex;gap:16px;flex-wrap:wrap;margin:12px 0}.legend span{display:flex;align-items:center;gap:6px}.legend i{width:10px;height:10px;border-radius:2px}svg{display:block;width:100%;max-width:920px;height:auto;border:1px solid var(--vscode-panel-border,#d0d7de);background:var(--vscode-editor-background,#fff)}svg text{font-size:11px;fill:var(--vscode-descriptionForeground,#57606a)}.grid{stroke:var(--vscode-panel-border,#d0d7de);stroke-width:1}.table-wrap{overflow:auto;margin-top:12px}table{border-collapse:collapse;width:100%;font-size:12px}th,td{text-align:left;border-bottom:1px solid var(--vscode-panel-border,#d0d7de);padding:7px 8px;white-space:nowrap}code{font-family:var(--vscode-editor-font-family,monospace)}</style></head><body><h1>${escapeHtml(labels.title)}</h1><p class="muted">${escapeHtml(labels.bounded)}</p>${sections || `<p>${escapeHtml(labels.empty)}</p>`}<h2>${escapeHtml(labels.limitations)}</h2><ul>${(trend.limitations || []).map((item) => `<li>${escapeHtml(item)}</li>`).join("")}</ul></body></html>`;
}

function writeDevicePerformanceTrend(buildRoot, trend, html) {
  const directory = path.resolve(buildRoot, "performance", "trends");
  fs.mkdirSync(directory, { recursive: true });
  const name = safeName(trend.app?.key, "app");
  const jsonPath = path.join(directory, `${name}.device-trend.json`);
  const htmlPath = path.join(directory, `${name}.device-trend.html`);
  writeJson(jsonPath, trend);
  fs.writeFileSync(htmlPath, html, "utf8");
  return { json: jsonPath, html: htmlPath };
}

module.exports = {
  HISTORY_FORMAT,
  HISTORY_LIMIT,
  MAX_INPUT_BYTES,
  SESSION_FORMAT,
  TREND_FORMAT,
  appKeyForRoot,
  artifactKind,
  createPerformanceSession,
  defaultArtifactPaths,
  discoverPerformanceArtifacts,
  finalizePerformanceSession,
  historyFiles,
  performanceInputsFromPaths,
  performanceProfile,
  comparePerformanceProfiles,
  readPerformanceHistory,
  buildDevicePerformanceTrend,
  renderDevicePerformanceTrendHtml,
  writeDevicePerformanceTrend
};
