const fs = require("fs");
const path = require("path");

const SDK_MANIFEST_FILENAME = "sdk-manifest.json";
const SDK_INSTALL_METADATA_FILENAME = ".jellyframe-sdk-install.json";
const RELEASE_VERSION_PATTERN = /^([0-9]+\.[0-9]+\.[0-9]+)(?:-|$)/;
const RENDER_CORE_LOCK_PATTERN = /^set\(JELLYFRAME_RENDER_CORE_LOCKED_VERSION\s+"([0-9]+\.[0-9]+\.[0-9]+)"\)/m;
const RENDER_CORE_ABI_PATTERN = /^set\(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI\s+"([0-9]+)"\)/m;

function existingDirectory(value) {
  if (!value) {
    return undefined;
  }
  try {
    return fs.statSync(value).isDirectory() ? path.resolve(value) : undefined;
  } catch (_) {
    return undefined;
  }
}

function isInside(child, parent) {
  const relative = path.relative(path.resolve(parent), path.resolve(child));
  return relative === "" || (relative !== ".." && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative));
}

function isSdkRoot(root) {
  const candidate = existingDirectory(root);
  return Boolean(candidate && fs.existsSync(path.join(candidate, "tools", "jellyframe_cli.py")));
}

function findSdkRootFrom(startPath) {
  if (!startPath) {
    return undefined;
  }
  let current = existingDirectory(startPath);
  if (!current) {
    try {
      current = fs.statSync(startPath).isDirectory() ? path.resolve(startPath) : path.dirname(path.resolve(startPath));
    } catch (_) {
      return undefined;
    }
  }
  while (true) {
    if (isSdkRoot(current)) {
      return current;
    }
    const parent = path.dirname(current);
    if (parent === current) {
      return undefined;
    }
    current = parent;
  }
}

function readProjectDescriptor(workspaceRoot) {
  if (!workspaceRoot) {
    return undefined;
  }
  const descriptor = path.join(workspaceRoot, ".jellyframe", "project.json");
  try {
    const value = JSON.parse(fs.readFileSync(descriptor, "utf8"));
    if (!value || value.format !== "jellyframe.app.project" || value.formatVersion !== 1) {
      return undefined;
    }
    return value;
  } catch (_) {
    return undefined;
  }
}

function sdkRootFromDescriptor(workspaceRoot, descriptor) {
  if (!descriptor || typeof descriptor.sdkRoot !== "string" || !descriptor.sdkRoot.trim()) {
    return undefined;
  }
  const configured = path.isAbsolute(descriptor.sdkRoot)
    ? descriptor.sdkRoot
    : path.resolve(workspaceRoot, descriptor.sdkRoot);
  return isSdkRoot(configured) ? configured : undefined;
}

function resolveSdkRoot({ workspaceRoot, configuredRoot, extensionPath, env = process.env } = {}) {
  const descriptor = readProjectDescriptor(workspaceRoot);
  const projectRoot = sdkRootFromDescriptor(workspaceRoot, descriptor);
  if (projectRoot) {
    return projectRoot;
  }
  const configured = findSdkRootFrom(configuredRoot);
  if (configured) {
    return configured;
  }
  const environment = findSdkRootFrom(env.JELLYFRAME_SDK_ROOT);
  if (environment) {
    return environment;
  }
  const workspace = findSdkRootFrom(workspaceRoot);
  if (workspace) {
    return workspace;
  }
  return findSdkRootFrom(extensionPath);
}

function resolvePython({ sdkRoot, configuredPath, platform = process.platform } = {}) {
  const override = String(configuredPath || "").trim();
  if (override) return override;
  if (sdkRoot && platform === "win32") {
    const bundled = path.join(sdkRoot, "runtime", "python", "python.exe");
    if (fs.existsSync(bundled)) return bundled;
  }
  return platform === "win32" ? "python" : "python3";
}

function authorOutputRoot(workspaceRoot, sdkRoot) {
  if (workspaceRoot && sdkRoot && !isInside(workspaceRoot, sdkRoot)) {
    return path.join(workspaceRoot, ".jellyframe", "build");
  }
  return sdkRoot ? path.join(sdkRoot, "build") : undefined;
}

function readJsonObject(filename) {
  try {
    const value = JSON.parse(fs.readFileSync(filename, "utf8"));
    return value && typeof value === "object" && !Array.isArray(value) ? value : undefined;
  } catch (_) {
    return undefined;
  }
}

function releaseVersion(value) {
  if (typeof value !== "string") {
    return undefined;
  }
  return RELEASE_VERSION_PATTERN.exec(value.trim())?.[1];
}

function readRenderCoreLock(root) {
  try {
    const text = fs.readFileSync(path.join(root, "cmake", "jellyframe_dependency_lock.cmake"), "utf8");
    const version = RENDER_CORE_LOCK_PATTERN.exec(text)?.[1];
    const abiText = RENDER_CORE_ABI_PATTERN.exec(text)?.[1];
    return {
      version,
      abi: abiText === undefined ? undefined : Number(abiText)
    };
  } catch (_) {
    return {};
  }
}

function sdkManifestCompatibility(metadata, manifest) {
  const runtime = manifest?.runtime;
  if (!metadata || !runtime || typeof runtime !== "object" || Array.isArray(runtime)) {
    return { compatible: true, issues: [] };
  }
  const issues = [];
  const requiredRuntime = releaseVersion(runtime.minJellyFrame);
  const sdkRuntime = releaseVersion(metadata.runtimeVersion);
  if (requiredRuntime && sdkRuntime && requiredRuntime !== sdkRuntime) {
    issues.push({
      code: "runtime-version-mismatch",
      required: requiredRuntime,
      actual: sdkRuntime
    });
  }
  const requiredCore = releaseVersion(runtime.minRenderCore);
  const sdkCore = releaseVersion(metadata.renderCoreVersion);
  if (requiredCore && sdkCore && requiredCore !== sdkCore) {
    issues.push({
      code: "render-core-version-mismatch",
      required: requiredCore,
      actual: sdkCore
    });
  }
  return { compatible: issues.length === 0, issues };
}

function readSdkMetadata(root) {
  if (!isSdkRoot(root)) {
    return undefined;
  }
  const resolved = path.resolve(root);
  const manifest = readJsonObject(path.join(resolved, SDK_MANIFEST_FILENAME));
  const install = readJsonObject(path.join(resolved, SDK_INSTALL_METADATA_FILENAME));
  const sourceVersion = (() => {
    try {
      return fs.readFileSync(path.join(resolved, "VERSION"), "utf8").trim() || undefined;
    } catch (_) {
      return undefined;
    }
  })();
  const packaged = manifest?.format === "jellyframe.app-author-sdk" && manifest.formatVersion === 1;
  const renderCore = readRenderCoreLock(resolved);
  return {
    root: resolved,
    kind: packaged ? "app-sdk" : "source-checkout",
    runtimeVersion: typeof manifest?.runtimeVersion === "string" ? manifest.runtimeVersion : sourceVersion,
    renderCoreVersion: renderCore.version,
    renderCoreAbi: renderCore.abi,
    releaseTag: typeof install?.releaseTag === "string" ? install.releaseTag : undefined,
    desktopProfiles: manifest?.desktopProfiles && typeof manifest.desktopProfiles === "object"
      ? Object.keys(manifest.desktopProfiles).sort()
      : []
  };
}

module.exports = {
  resolvePython,
  authorOutputRoot,
  findSdkRootFrom,
  isInside,
  isSdkRoot,
  readSdkMetadata,
  readProjectDescriptor,
  resolveSdkRoot,
  sdkManifestCompatibility,
  SDK_INSTALL_METADATA_FILENAME
};
