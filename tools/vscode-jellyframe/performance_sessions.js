const crypto = require("crypto");
const fs = require("fs");
const path = require("path");

const SESSION_FORMAT = "jellyframe.performance.session";
const HISTORY_FORMAT = "jellyframe.performance.history";
const HISTORY_LIMIT = 20;
const MAX_INPUT_BYTES = 64 * 1024 * 1024;
const MAX_SESSION_INPUT_BYTES = 128 * 1024 * 1024;
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
  const manifest = {
    ...session.manifest,
    status: success ? "complete" : "failed",
    finishedAt,
    ...(success ? {} : { error: String(error || "performance report generation failed").slice(0, 500) })
  };
  writeJson(session.manifestPath, manifest);
  session.manifest = manifest;

  const history = readPerformanceHistory(path.dirname(session.performanceRoot));
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

module.exports = {
  HISTORY_FORMAT,
  HISTORY_LIMIT,
  MAX_INPUT_BYTES,
  SESSION_FORMAT,
  appKeyForRoot,
  artifactKind,
  createPerformanceSession,
  defaultArtifactPaths,
  discoverPerformanceArtifacts,
  finalizePerformanceSession,
  historyFiles,
  performanceInputsFromPaths,
  readPerformanceHistory
};
