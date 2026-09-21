const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const {
  parseCatalog,
  selectProviderEntry,
  selectReleaseAsset,
  sha256
} = require("../../tools/vscode-jellyframe/device_provider_download");

const catalog = JSON.parse(fs.readFileSync(
  path.join(__dirname, "../../tools/device-providers/catalog.json"),
  "utf8"
));
const entry = selectProviderEntry(catalog, "ws147-developer-0.6.2-ws147.2");
assert.strictEqual(parseCatalog(catalog), catalog);
assert.strictEqual(entry.releaseTag, "device-provider-ws147-v0.1.1-dev");
assert.strictEqual(entry.assetName, "jellyframe-ws147-developer-0.6.2-ws147.2-provider-0.1.1-dev.zip");
assert.match(entry.sha256, /^[0-9a-f]{64}$/);

const asset = {
  name: entry.assetName,
  browser_download_url: `https://github.com/xiaopaoya/JellyFrame/releases/download/${entry.releaseTag}/${entry.assetName}`
};
assert.deepStrictEqual(selectReleaseAsset({ assets: [asset] }, entry), asset);
assert.throws(
  () => selectReleaseAsset({ assets: [] }, entry),
  /does not publish the expected provider archive/
);
assert.throws(
  () => parseCatalog({ format: "jellyframe.device-provider-catalog", formatVersion: 1, entries: [] }),
  /invalid format/
);
assert.throws(
  () => parseCatalog({
    format: "jellyframe.device-provider-catalog",
    formatVersion: 1,
    entries: [{ ...entry, id: entry.id, sha256: "bad" }]
  }),
  /invalid entry/
);

const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "jellyframe-provider-download-test-"));
try {
  const file = path.join(temporary, "payload.bin");
  fs.writeFileSync(file, Buffer.from("jellyframe-provider"));
  assert.strictEqual(sha256(file), "2d31df15c34b478f04ac3d8f532f9feeb698960a0e182ec5832a88768a4b8ac1");
} finally {
  fs.rmSync(temporary, { recursive: true, force: true });
}
console.log("VS Code Device Provider download helper tests passed");
