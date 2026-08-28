const fs = require("fs");
const path = require("path");

const SDK_MANIFEST_FILENAME = "sdk-manifest.json";
const SDK_INSTALL_METADATA_FILENAME = ".jellyframe-sdk-install.json";

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
  return {
    root: resolved,
    kind: packaged ? "app-sdk" : "source-checkout",
    runtimeVersion: typeof manifest?.runtimeVersion === "string" ? manifest.runtimeVersion : sourceVersion,
    releaseTag: typeof install?.releaseTag === "string" ? install.releaseTag : undefined,
    desktopProfiles: manifest?.desktopProfiles && typeof manifest.desktopProfiles === "object"
      ? Object.keys(manifest.desktopProfiles).sort()
      : []
  };
}

module.exports = {
  authorOutputRoot,
  findSdkRootFrom,
  isInside,
  isSdkRoot,
  readSdkMetadata,
  readProjectDescriptor,
  resolveSdkRoot,
  SDK_INSTALL_METADATA_FILENAME
};
