const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const {
  desktopBuildPlan,
  jerryscriptBuildArguments,
  jerryscriptState,
  managedProfileRoot
} = require("../../tools/vscode-jellyframe/desktop_build_setup");

function main() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "jellyframe-vscode-build-"));
  try {
    const source = path.join(root, "third_party", "jerryscript");
    const plan = desktopBuildPlan(root, { scripting: true, jerryscriptRoot: source });
    assert.match(plan.buildRoot, /desktop-scripting-release$/);
    assert.match(plan.outputDirectory, /desktop-scripting-release[\\/]Release$/);
    assert.ok(plan.configureArguments.includes("-DJELLYFRAME_BUILD_SCRIPTING=ON"));
    assert.ok(plan.configureArguments.includes(`-DJERRYSCRIPT_ROOT=${source}`));
    if (process.platform === "win32") {
      assert.ok(plan.configureArguments.includes("Visual Studio 17 2022"));
    }
    assert.deepEqual(plan.buildArguments.slice(-2), ["--target", "jellyframe_desktop_shell"]);
    const standardPlan = desktopBuildPlan(root);
    assert.match(standardPlan.buildRoot, /desktop-release$/);
    assert.equal(standardPlan.configureArguments.includes("-DJELLYFRAME_BUILD_SCRIPTING=ON"), false);

    fs.mkdirSync(path.join(source, "jerry-core", "include"), { recursive: true });
    fs.writeFileSync(path.join(source, "jerry-core", "include", "jerryscript.h"), "", "utf8");
    for (const library of ["jerry-core", "jerry-ext", "jerry-port"]) {
      const directory = path.join(source, "build", "lib", "MinSizeRel");
      fs.mkdirSync(directory, { recursive: true });
      fs.writeFileSync(path.join(directory, `${library}.lib`), "", "utf8");
    }
    const state = jerryscriptState(root);
    assert.equal(state.sourceAvailable, true);
    assert.equal(state.librariesAvailable, true);
    assert.equal(state.libraryConfiguration, "MinSizeRel");
    assert.deepEqual(jerryscriptBuildArguments(source).slice(-2), ["--clean", "--cmake-param=-DJERRY_VM_HALT=ON"]);

    assert.equal(managedProfileRoot(root, plan.outputDirectory), plan.buildRoot);
    assert.equal(managedProfileRoot(root, path.join(root, "outside", "Release")), undefined);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
}

main();
