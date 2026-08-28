const fs = require("fs");
const path = require("path");

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

module.exports = {
  authorOutputRoot,
  findSdkRootFrom,
  isInside,
  isSdkRoot,
  readProjectDescriptor,
  resolveSdkRoot
};
