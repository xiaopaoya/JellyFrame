"use strict";

const crypto = require("crypto");
const fs = require("fs");
const https = require("https");
const os = require("os");
const path = require("path");

const REPOSITORY = "xiaopaoya/JellyFrame";
const CATALOG_URL = "https://raw.githubusercontent.com/xiaopaoya/JellyFrame/master/tools/device-providers/catalog.json";
const API_HOST = "api.github.com";
const MAX_METADATA_BYTES = 2 * 1024 * 1024;
const MAX_ARCHIVE_BYTES = 512 * 1024 * 1024;
const MAX_REDIRECTS = 4;
const SHA256_PATTERN = /^[0-9a-f]{64}$/i;

function assertGithubUrl(value) {
  const url = new URL(value);
  const hosts = [
    API_HOST,
    "raw.githubusercontent.com",
    "github.com",
    "objects.githubusercontent.com",
    "release-assets.githubusercontent.com"
  ];
  if (url.protocol !== "https:" || !hosts.includes(url.hostname)) {
    throw new Error(`refusing non-GitHub HTTPS URL: ${url.hostname}`);
  }
  return url;
}

function request(urlValue, { onData, maxBytes = MAX_METADATA_BYTES } = {}, redirects = 0) {
  const url = assertGithubUrl(urlValue);
  return new Promise((resolve, reject) => {
    const requestHandle = https.get(url, {
      headers: {
        Accept: "application/vnd.github+json",
        "User-Agent": "JellyFrame-VSCode-Tools"
      }
    }, (response) => {
      const status = response.statusCode || 0;
      if (status >= 300 && status < 400 && response.headers.location) {
        response.resume();
        if (redirects >= MAX_REDIRECTS) {
          reject(new Error("too many GitHub redirects"));
          return;
        }
        const next = new URL(response.headers.location, url).toString();
        request(next, { onData, maxBytes }, redirects + 1).then(resolve, reject);
        return;
      }
      if (status < 200 || status >= 300) {
        response.resume();
        reject(new Error(`GitHub returned HTTP ${status}`));
        return;
      }
      let received = 0;
      const chunks = [];
      let exceeded = false;
      response.on("data", (chunk) => {
        received += chunk.length;
        if (received > maxBytes) {
          exceeded = true;
          response.destroy(new Error("GitHub response exceeds the size limit"));
          return;
        }
        if (onData) {
          onData(chunk, received, Number(response.headers["content-length"] || 0), response);
        } else {
          chunks.push(chunk);
        }
      });
      response.on("end", () => {
        if (!exceeded) {
          resolve(Buffer.concat(chunks));
        }
      });
      response.on("error", reject);
    });
    requestHandle.setTimeout(30000, () => requestHandle.destroy(new Error("GitHub request timed out")));
    requestHandle.on("error", reject);
  });
}

function sha256(pathname) {
  const digest = crypto.createHash("sha256");
  const handle = fs.openSync(pathname, "r");
  try {
    const buffer = Buffer.allocUnsafe(64 * 1024);
    let read;
    do {
      read = fs.readSync(handle, buffer, 0, buffer.length, null);
      if (read > 0) digest.update(buffer.subarray(0, read));
    } while (read > 0);
  } finally {
    fs.closeSync(handle);
  }
  return digest.digest("hex");
}

function parseCatalog(value) {
  const catalog = typeof value === "string" ? JSON.parse(value) : value;
  if (catalog?.format !== "jellyframe.device-provider-catalog" || catalog?.formatVersion !== 1 ||
      !Array.isArray(catalog.entries) || catalog.entries.length === 0) {
    throw new Error("the official Device Provider catalog has an invalid format");
  }
  const ids = new Set();
  for (const entry of catalog.entries) {
    if (!entry || typeof entry.id !== "string" || ids.has(entry.id) ||
        typeof entry.releaseTag !== "string" || typeof entry.assetName !== "string" ||
        !SHA256_PATTERN.test(String(entry.sha256 || ""))) {
      throw new Error("the official Device Provider catalog contains an invalid entry");
    }
    ids.add(entry.id);
  }
  return catalog;
}

function selectProviderEntry(catalog, id) {
  const parsed = parseCatalog(catalog);
  const entry = parsed.entries.find((candidate) => candidate.id === id);
  if (!entry) {
    throw new Error(`Device Provider catalog entry not found: ${id}`);
  }
  return entry;
}

function selectReleaseAsset(release, entry) {
  const assets = Array.isArray(release?.assets) ? release.assets : [];
  const matches = assets.filter((asset) => asset?.name === entry.assetName);
  if (matches.length !== 1 || !matches[0].browser_download_url) {
    throw new Error(`GitHub release ${entry.releaseTag} does not publish the expected provider archive`);
  }
  return matches[0];
}

async function fetchProviderCatalog() {
  const bytes = await request(CATALOG_URL);
  return parseCatalog(bytes.toString("utf8"));
}

async function downloadProvider(entry, { onProgress } = {}) {
  const releaseBytes = await request(`https://${API_HOST}/repos/${REPOSITORY}/releases/tags/${encodeURIComponent(entry.releaseTag)}`);
  const release = JSON.parse(releaseBytes.toString("utf8"));
  const asset = selectReleaseAsset(release, entry);
  const temporaryDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "jellyframe-provider-download-"));
  const archivePath = path.join(temporaryDirectory, asset.name);
  try {
    const stream = fs.createWriteStream(archivePath, { flags: "wx" });
    let streamError;
    stream.on("error", (error) => { streamError = error; });
    await request(asset.browser_download_url, {
      maxBytes: MAX_ARCHIVE_BYTES,
      onData: (chunk, received, total, response) => {
        if (!stream.write(chunk)) {
          response.pause();
          stream.once("drain", () => response.resume());
        }
        onProgress?.({ received, total });
      }
    });
    if (streamError) throw streamError;
    await new Promise((resolve, reject) => {
      stream.end((error) => error ? reject(error) : resolve());
      stream.on("error", reject);
    });
    const actualDigest = sha256(archivePath);
    const expectedDigest = String(entry.sha256).toLowerCase();
    if (actualDigest !== expectedDigest) {
      throw new Error(`Device Provider SHA-256 mismatch: expected ${expectedDigest}, received ${actualDigest}`);
    }
    return {
      archivePath,
      temporaryDirectory,
      assetName: asset.name,
      releaseTag: entry.releaseTag,
      expectedDigest,
      bytes: fs.statSync(archivePath).size
    };
  } catch (error) {
    fs.rmSync(temporaryDirectory, { recursive: true, force: true });
    throw error;
  }
}

module.exports = {
  CATALOG_URL,
  REPOSITORY,
  parseCatalog,
  selectProviderEntry,
  selectReleaseAsset,
  fetchProviderCatalog,
  downloadProvider,
  sha256
};
