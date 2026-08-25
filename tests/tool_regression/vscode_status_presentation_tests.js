const assert = require("assert");
const { desktopBuildPresentation } = require("../../tools/vscode-jellyframe/status_presentation");

function main() {
  const script = desktopBuildPresentation(
    "C:\\work\\JellyFrame\\build\\desktop-scripting-release\\Release", true
  );
  assert.deepEqual(script, {
    summary: "脚本桌面壳 · Release",
    profile: "desktop-scripting-release · Release",
    output: "C:\\work\\JellyFrame\\build\\desktop-scripting-release\\Release",
    scripting: "已启用"
  });

  const ordinary = desktopBuildPresentation("/work/JellyFrame/build/desktop-debug/Debug", false);
  assert.equal(ordinary.summary, "Standard desktop shell · Debug");
  assert.equal(ordinary.scripting, "Not enabled");
  assert.equal(desktopBuildPresentation(undefined, false).summary, "No compatible build");
}

main();
