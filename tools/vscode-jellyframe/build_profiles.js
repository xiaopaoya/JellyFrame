const fs = require("fs");
const path = require("path");

const LEGACY_SCRIPT_TASK_OPTION = "JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME";

function cmakeBoolean(value) {
  return /^(1|on|true|yes)$/i.test(String(value || "").trim());
}

function cmakeCacheFor(buildDirectory) {
  let candidate = path.resolve(buildDirectory);
  while (true) {
    const cache = path.join(candidate, "CMakeCache.txt");
    if (fs.existsSync(cache)) {
      return cache;
    }
    const parent = path.dirname(candidate);
    if (parent === candidate) {
      return undefined;
    }
    candidate = parent;
  }
}

function readCmakeCache(cachePath) {
  const values = new Map();
  for (const line of fs.readFileSync(cachePath, "utf8").split(/\r?\n/)) {
    const match = line.match(/^([^:=#]+):[^=]*=(.*)$/);
    if (match) {
      values.set(match[1], match[2]);
    }
  }
  return values;
}

function buildDirectoryIssue(buildDirectory, requiresScripting) {
  if (!fs.existsSync(buildDirectory)) {
    return { code: "missing-directory" };
  }
  const cachePath = cmakeCacheFor(buildDirectory);
  if (!cachePath) {
    return { code: "missing-cache" };
  }
  const cache = readCmakeCache(cachePath);
  if (cache.has(LEGACY_SCRIPT_TASK_OPTION)) {
    return { code: "legacy-script-task-option", cachePath };
  }
  if (requiresScripting && !cmakeBoolean(cache.get("JELLYFRAME_BUILD_SCRIPTING"))) {
    return { code: "scripting-disabled", cachePath };
  }
  return undefined;
}

function buildCandidates(repositoryRoot, requiresScripting) {
  const build = path.join(repositoryRoot, "build");
  if (requiresScripting) {
    return [
      path.join(build, "desktop-scripting-release", "Release"),
      path.join(build, "desktop-scripting-debug", "Debug")
    ];
  }
  return [
    path.join(build, "desktop-release", "Release"),
    path.join(build, "desktop-debug", "Debug")
  ];
}

function selectBuildDirectory(repositoryRoot, configuredDirectory, requiresScripting) {
  if (configuredDirectory) {
    const issue = buildDirectoryIssue(configuredDirectory, requiresScripting);
    return issue
      ? { issue, configured: true }
      : { buildDirectory: configuredDirectory, configured: true };
  }

  const rejected = [];
  for (const candidate of buildCandidates(repositoryRoot, requiresScripting)) {
    if (!fs.existsSync(candidate)) {
      continue;
    }
    const issue = buildDirectoryIssue(candidate, requiresScripting);
    if (!issue) {
      return { buildDirectory: candidate, configured: false };
    }
    rejected.push({ candidate, issue });
  }
  return { issue: { code: "no-compatible-build", requiresScripting, rejected }, configured: false };
}

module.exports = {
  LEGACY_SCRIPT_TASK_OPTION,
  buildCandidates,
  buildDirectoryIssue,
  cmakeCacheFor,
  readCmakeCache,
  selectBuildDirectory
};
