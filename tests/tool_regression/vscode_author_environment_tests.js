const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const {
  authorOutputRoot,
  findSdkRootFrom,
  isInside,
  readSdkMetadata,
  readProjectDescriptor,
  resolveSdkRoot,
  sdkManifestCompatibility
} = require("../../tools/vscode-jellyframe/author_environment");

function main() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "jellyframe-author-env-"));
  try {
    const sdk = path.join(root, "sdk");
    const workspace = path.join(root, "my-app");
    fs.mkdirSync(path.join(sdk, "tools"), { recursive: true });
    fs.mkdirSync(workspace, { recursive: true });
    fs.writeFileSync(path.join(sdk, "tools", "jellyframe_cli.py"), "", "utf8");
    fs.mkdirSync(path.join(sdk, "cmake"), { recursive: true });
    fs.writeFileSync(path.join(sdk, "cmake", "jellyframe_dependency_lock.cmake"),
      'set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "0.6.2")\n' +
      'set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "1")\n', "utf8");
    fs.writeFileSync(path.join(sdk, "sdk-manifest.json"), JSON.stringify({
      format: "jellyframe.app-author-sdk",
      formatVersion: 1,
      runtimeVersion: "0.6.0-dev",
      desktopProfiles: { "desktop-release": {} }
    }), "utf8");
    fs.writeFileSync(path.join(sdk, ".jellyframe-sdk-install.json"), JSON.stringify({
      format: "jellyframe.sdk-install",
      formatVersion: 1,
      releaseTag: "app-sdk-v0.6.0-dev.1"
    }), "utf8");

    assert.equal(findSdkRootFrom(path.join(workspace, "jellyframe.app.json")), undefined);
    fs.mkdirSync(path.join(workspace, ".jellyframe"), { recursive: true });
    fs.writeFileSync(path.join(workspace, ".jellyframe", "project.json"), JSON.stringify({
      format: "jellyframe.app.project",
      formatVersion: 1,
      sdkRoot: path.relative(workspace, sdk)
    }), "utf8");
    assert.equal(readProjectDescriptor(workspace).formatVersion, 1);
    assert.deepStrictEqual(readSdkMetadata(sdk), {
      root: path.resolve(sdk),
      kind: "app-sdk",
      runtimeVersion: "0.6.0-dev",
      renderCoreVersion: "0.6.2",
      renderCoreAbi: 1,
      releaseTag: "app-sdk-v0.6.0-dev.1",
      desktopProfiles: ["desktop-release"]
    });
    assert.deepStrictEqual(sdkManifestCompatibility(readSdkMetadata(sdk), {
      runtime: { minJellyFrame: "0.6.0", minRenderCore: "0.6.2" }
    }), { compatible: true, issues: [] });
    assert.deepStrictEqual(sdkManifestCompatibility(readSdkMetadata(sdk), {
      runtime: { minJellyFrame: "0.6.0", minRenderCore: "0.6.3" }
    }), {
      compatible: false,
      issues: [{ code: "render-core-version-mismatch", required: "0.6.3", actual: "0.6.2" }]
    });
    assert.deepStrictEqual(sdkManifestCompatibility(readSdkMetadata(sdk), {
      runtime: { minJellyFrame: "0.7.0", minRenderCore: "0.6.2" }
    }), {
      compatible: false,
      issues: [{ code: "runtime-version-mismatch", required: "0.7.0", actual: "0.6.0" }]
    });
    assert.deepStrictEqual(sdkManifestCompatibility({
      runtimeVersion: "0.6.0-dev",
      renderCoreVersion: "0.6.1"
    }, {
      runtime: { minJellyFrame: "0.6.0", minRenderCore: "0.6.2" }
    }), {
      compatible: false,
      issues: [{ code: "render-core-version-mismatch", required: "0.6.2", actual: "0.6.1" }]
    });
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
