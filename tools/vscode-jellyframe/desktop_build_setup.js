"use strict";

const fs = require("fs");
const path = require("path");

const SCRIPTING_PROFILE = "desktop-scripting-release";
const STANDARD_PROFILE = "desktop-release";
const DESKTOP_CONFIGURATION = "Release";

function desktopBuildPlan(repositoryRoot, { scripting = false, jerryscriptRoot = "" } = {}) {
  const profile = scripting ? SCRIPTING_PROFILE : STANDARD_PROFILE;
  const buildRoot = path.join(repositoryRoot, "build", profile);
  const outputDirectory = path.join(buildRoot, DESKTOP_CONFIGURATION);
  const configureArguments = ["-S", repositoryRoot, "-B", buildRoot];
  if (process.platform === "win32") {
    configureArguments.push("-G", "Visual Studio 17 2022", "-A", "x64");
  }
  if (scripting) {
    configureArguments.push("-DJELLYFRAME_BUILD_SCRIPTING=ON");
    if (jerryscriptRoot) {
      configureArguments.push(`-DJERRYSCRIPT_ROOT=${jerryscriptRoot}`);
    }
  }
  return {
    profile,
    buildRoot,
    outputDirectory,
    configureArguments,
    buildArguments: ["--build", buildRoot, "--config", DESKTOP_CONFIGURATION, "--target", "jellyframe_desktop_shell"]
  };
}

function jerryscriptState(repositoryRoot) {
  const sourceDirectory = path.join(repositoryRoot, "third_party", "jerryscript");
  const header = path.join(sourceDirectory, "jerry-core", "include", "jerryscript.h");
  const configurations = ["Release", "MinSizeRel", "RelWithDebInfo", "Debug"];
  const libraryConfiguration = configurations.find((configuration) => ["jerry-core", "jerry-ext", "jerry-port"].every(
    (library) => fs.existsSync(path.join(sourceDirectory, "build", "lib", configuration, `${library}.lib`))
  ));
  return {
    sourceDirectory,
    sourceAvailable: fs.existsSync(header),
    librariesAvailable: Boolean(libraryConfiguration),
    libraryConfiguration
  };
}

function jerryscriptBuildArguments(sourceDirectory) {
  return [
    path.join(sourceDirectory, "tools", "build.py"),
    "--clean",
    "--cmake-param=-DJERRY_VM_HALT=ON"
  ];
}

function managedProfileRoot(repositoryRoot, outputDirectory) {
  const buildRoot = path.resolve(repositoryRoot, "build");
  const profileRoot = path.dirname(path.resolve(outputDirectory));
  const relative = path.relative(buildRoot, profileRoot);
  if (!relative || relative.startsWith("..") || path.isAbsolute(relative)) {
    return undefined;
  }
  return /^desktop(?:-scripting)?-(?:release|debug)$/i.test(path.basename(profileRoot))
    ? profileRoot
    : undefined;
}

module.exports = {
  DESKTOP_CONFIGURATION,
  SCRIPTING_PROFILE,
  STANDARD_PROFILE,
  desktopBuildPlan,
  jerryscriptBuildArguments,
  jerryscriptState,
  managedProfileRoot
};
