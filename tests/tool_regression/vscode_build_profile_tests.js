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
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
}

main();
