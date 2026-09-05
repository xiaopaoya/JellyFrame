const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { selectBuildDirectory } = require("../../tools/vscode-jellyframe/build_profiles");

function makeProfile(root, name, configuration, cacheEntries) {
  const buildRoot = path.join(root, "build", name);
  const output = path.join(buildRoot, configuration);
  fs.mkdirSync(output, { recursive: true });
  fs.writeFileSync(
    path.join(buildRoot, "CMakeCache.txt"),
    Object.entries(cacheEntries).map(([key, value]) => `${key}:BOOL=${value}`).join("\n") + "\n",
    "utf8"
  );
  return output;
}

function makePrebuiltSdkProfile(root, name) {
  const output = path.join(root, "build", name, "Release");
  fs.mkdirSync(path.join(root, "tools"), { recursive: true });
  fs.mkdirSync(output, { recursive: true });
  fs.writeFileSync(path.join(root, "tools", "jellyframe_cli.py"), "", "utf8");
  fs.writeFileSync(path.join(output, process.platform === "win32" ? "jellyframe_desktop_shell.exe" : "jellyframe_desktop_shell"), "", "utf8");
  fs.writeFileSync(path.join(root, "sdk-manifest.json"), JSON.stringify({
    format: "jellyframe.app-author-sdk",
    formatVersion: 1,
    desktopProfiles: { [name]: { tools: [process.platform === "win32" ? "jellyframe_desktop_shell.exe" : "jellyframe_desktop_shell"] } }
  }), "utf8");
  return output;
}

function main() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "jellyframe-vscode-profile-"));
  try {
    const ordinary = makeProfile(root, "desktop-release", "Release", {
      JELLYFRAME_BUILD_SCRIPTING: "OFF"
    });
    const staleScripting = makeProfile(root, "desktop-scripting-release", "Release", {
      JELLYFRAME_BUILD_SCRIPTING: "ON",
      JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME: "ON"
    });
    const currentScripting = makeProfile(root, "desktop-scripting-debug", "Debug", {
      JELLYFRAME_BUILD_SCRIPTING: "ON",
      JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME: "ON"
    });

    assert.equal(selectBuildDirectory(root, "", false).buildDirectory, ordinary);
    assert.equal(selectBuildDirectory(root, "", true).buildDirectory, currentScripting);
    assert.equal(
      selectBuildDirectory(root, staleScripting, true).issue.code,
      "legacy-script-task-option"
    );
    assert.equal(
      selectBuildDirectory(root, ordinary, true).issue.code,
      "scripting-disabled"
    );

    fs.rmSync(currentScripting, { recursive: true, force: true });
    assert.equal(selectBuildDirectory(root, "", true).issue.code, "no-compatible-build");

    const sdk = path.join(root, "sdk");
    const prebuiltStandard = makePrebuiltSdkProfile(sdk, "desktop-release");
    assert.equal(selectBuildDirectory(sdk, "", false).buildDirectory, prebuiltStandard);
    assert.equal(selectBuildDirectory(sdk, "", true).issue.code, "no-compatible-build");
    const prebuiltScripting = makePrebuiltSdkProfile(sdk, "desktop-scripting-release");
    assert.equal(selectBuildDirectory(sdk, "", true).buildDirectory, prebuiltScripting);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
}

main();
