const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const {
  authorOutputRoot,
  findSdkRootFrom,
  isInside,
  readProjectDescriptor,
  resolveSdkRoot
} = require("../../tools/vscode-jellyframe/author_environment");

function main() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "jellyframe-author-env-"));
  try {
    const sdk = path.join(root, "sdk");
    const workspace = path.join(root, "my-app");
    fs.mkdirSync(path.join(sdk, "tools"), { recursive: true });
    fs.mkdirSync(workspace, { recursive: true });
    fs.writeFileSync(path.join(sdk, "tools", "jellyframe_cli.py"), "", "utf8");

    assert.equal(findSdkRootFrom(path.join(workspace, "jellyframe.app.json")), undefined);
    fs.mkdirSync(path.join(workspace, ".jellyframe"), { recursive: true });
    fs.writeFileSync(path.join(workspace, ".jellyframe", "project.json"), JSON.stringify({
      format: "jellyframe.app.project",
      formatVersion: 1,
      sdkRoot: path.relative(workspace, sdk)
    }), "utf8");
    assert.equal(readProjectDescriptor(workspace).formatVersion, 1);
    assert.equal(resolveSdkRoot({ workspaceRoot: workspace, extensionPath: root }), path.resolve(sdk));
    const alternativeSdk = path.join(root, "alternative-sdk");
    fs.mkdirSync(path.join(alternativeSdk, "tools"), { recursive: true });
    fs.writeFileSync(path.join(alternativeSdk, "tools", "jellyframe_cli.py"), "", "utf8");
    assert.equal(
      resolveSdkRoot({ workspaceRoot: workspace, configuredRoot: alternativeSdk, extensionPath: root }),
      path.resolve(sdk),
      "a project-pinned SDK must take precedence over the machine default"
    );
    assert.equal(authorOutputRoot(workspace, sdk), path.join(workspace, ".jellyframe", "build"));
    assert.equal(authorOutputRoot(sdk, sdk), path.join(sdk, "build"));
    assert.equal(isInside(workspace, sdk), false);
    assert.equal(isInside(path.join(sdk, "build"), sdk), true);
    const repository = path.resolve(__dirname, "..", "..");
    assert.equal(
      fs.readFileSync(path.join(repository, "tools", "schemas", "jellyframe.app.schema.json"), "utf8"),
      fs.readFileSync(path.join(repository, "tools", "vscode-jellyframe", "schemas", "jellyframe.app.schema.json"), "utf8"),
      "the bundled VSIX schema must match the canonical SDK schema"
    );
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
}

main();
