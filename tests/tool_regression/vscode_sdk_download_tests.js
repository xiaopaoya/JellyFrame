const assert = require("assert");
const {
  SDK_ASSET_PATTERN,
  parseDigest,
  parseSidecar,
  sdkInstallName,
  sdkReleaseMetadata,
  selectSdkAsset,
  selectSdkRelease
} = require("../../tools/vscode-jellyframe/sdk_download");

const asset = {
  name: "jellyframe-app-sdk-0.6.0-dev.zip",
  browser_download_url: "https://github.com/xiaopaoya/JellyFrame/releases/download/v0.6.0-dev/jellyframe-app-sdk-0.6.0-dev.zip",
  digest: "sha256:" + "a".repeat(64)
};

assert(SDK_ASSET_PATTERN.test(asset.name));
assert.strictEqual(parseDigest(asset.digest), "a".repeat(64));
assert.strictEqual(parseDigest("sha512:" + "a".repeat(128)), undefined);
assert.strictEqual(parseSidecar("a".repeat(64) + "  sdk.zip\n"), "a".repeat(64));
assert.strictEqual(sdkInstallName(asset.name), "jellyframe-app-sdk-0.6.0-dev");
assert.deepStrictEqual(selectSdkAsset({ assets: [asset] }), asset);
assert.strictEqual(
  selectSdkRelease([{ assets: [] }, { tag_name: "app-sdk-v0.6.0-dev", assets: [asset] }]).release.tag_name,
  "app-sdk-v0.6.0-dev"
);
sdkReleaseMetadata({ tag_name: "app-sdk-v0.6.0-dev", assets: [asset] }, asset).then((metadata) => {
  assert.deepStrictEqual(metadata, {
    assetName: asset.name,
    expectedDigest: "a".repeat(64),
    releaseTag: "app-sdk-v0.6.0-dev"
  });
  console.log("VS Code SDK download helper tests passed");
}).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
assert.throws(
  () => selectSdkAsset({ assets: [] }),
  /does not publish exactly one App Author SDK ZIP/
);
assert.throws(
  () => selectSdkAsset({ assets: [asset, { ...asset, name: "jellyframe-app-sdk-0.6.0-dev-2.zip" }] }),
  /publishes more than one App Author SDK ZIP/
);
