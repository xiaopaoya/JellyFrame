"use strict";

const crypto = require("crypto");
const fs = require("fs");
const https = require("https");
const os = require("os");
const path = require("path");

const REPOSITORY = "xiaopaoya/JellyFrame";
const API_HOST = "api.github.com";
const MAX_METADATA_BYTES = 2 * 1024 * 1024;
const MAX_REDIRECTS = 4;
const SDK_ASSET_PATTERN = /^jellyframe-app-sdk-[A-Za-z0-9._-]+\.zip$/;

function assertGithubUrl(value) {
  const url = new URL(value);
  if (url.protocol !== "https:" || ![API_HOST, "github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"].includes(url.hostname)) {
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
        const nextRequest = request(next, { onData, maxBytes }, redirects + 1);
        nextRequest.then(resolve, reject);
        return;
      }
      if (status < 200 || status >= 300) {
        response.resume();
        reject(new Error(`GitHub returned HTTP ${status}`));
        return;
      }
      let received = 0;
      const chunks = [];
      response.on("data", (chunk) => {
        received += chunk.length;
        if (received > maxBytes) {
          response.destroy(new Error("GitHub response exceeds the size limit"));
          return;
        }
        if (onData) {
          onData(chunk, received, Number(response.headers["content-length"] || 0), response);
        } else {
          chunks.push(chunk);
        }
      });
      response.on("end", () => resolve(Buffer.concat(chunks)));
      response.on("error", reject);
    });
    requestHandle.setTimeout(30000, () => requestHandle.destroy(new Error("GitHub request timed out")));
    requestHandle.on("error", reject);
  });
}

function sha256(pathname) {
  const digest = crypto.createHash("sha256");
  for (const chunk of readChunks(pathname)) {
    digest.update(chunk);
  }
  return digest.digest("hex");
}

function* readChunks(pathname) {
  // The archive is already fully downloaded; hash it synchronously so
  // verification completes before the caller can install it.
  const handle = fs.openSync(pathname, "r");
  try {
    const buffer = Buffer.allocUnsafe(64 * 1024);
    let read;
    do {
      read = fs.readSync(handle, buffer, 0, buffer.length, null);
      if (read > 0) {
        yield buffer.subarray(0, read);
      }
    } while (read > 0);
  } finally {
    fs.closeSync(handle);
  }
}

function parseDigest(value) {
  const match = /^sha256:([0-9a-f]{64})$/i.exec(String(value || "").trim());
  return match ? match[1].toLowerCase() : undefined;
}

function parseSidecar(value) {
  const match = /\b([0-9a-f]{64})\b/i.exec(String(value || ""));
  return match ? match[1].toLowerCase() : undefined;
}

function selectSdkAsset(release) {
  const assets = Array.isArray(release?.assets) ? release.assets : [];
  const candidates = assets.filter((asset) => SDK_ASSET_PATTERN.test(String(asset?.name || "")));
  if (candidates.length !== 1) {
    throw new Error(candidates.length === 0
      ? "the latest GitHub release does not publish exactly one App Author SDK ZIP"
      : "the latest GitHub release publishes more than one App Author SDK ZIP");
  }
  const asset = candidates[0];
  if (!asset.browser_download_url) {
    throw new Error("the App Author SDK release asset has no download URL");
  }
  return asset;
}

function selectSdkRelease(releases) {
  if (!Array.isArray(releases)) {
    throw new Error("GitHub returned an invalid release list");
  }
  for (const release of releases) {
    try {
      return { release, asset: selectSdkAsset(release) };
    } catch (_) {
      // A repository may also publish Runtime, Core or provider releases.
    }
  }
  throw new Error("GitHub does not publish a verified App Author SDK release yet");
}

async function downloadLatestSdk({ onProgress } = {}) {
  const releaseBytes = await request(`https://${API_HOST}/repos/${REPOSITORY}/releases?per_page=20`);
  const { release, asset } = selectSdkRelease(JSON.parse(releaseBytes.toString("utf8")));
  let expected = parseDigest(asset.digest);
  if (!expected) {
    const assets = Array.isArray(release.assets) ? release.assets : [];
    const sidecar = assets.find((candidate) => candidate.name === `${asset.name}.sha256`);
    if (sidecar?.browser_download_url) {
      const sidecarBytes = await request(sidecar.browser_download_url);
      expected = parseSidecar(sidecarBytes.toString("utf8"));
    }
  }
  if (!expected) {
    throw new Error("the App Author SDK release has no valid SHA-256 digest; refusing installation");
  }

  const temporaryDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "jellyframe-sdk-download-"));
  const archivePath = path.join(temporaryDirectory, asset.name);
  try {
    const stream = fs.createWriteStream(archivePath, { flags: "wx" });
    let streamError;
    stream.on("error", (error) => { streamError = error; });
    await request(asset.browser_download_url, {
      maxBytes: 512 * 1024 * 1024,
      onData: (chunk, received, total, response) => {
        if (!stream.write(chunk)) {
          response.pause();
          stream.once("drain", () => response.resume());
        }
        onProgress?.({ received, total });
      }
    });
    if (streamError) {
      throw streamError;
    }
    await new Promise((resolve, reject) => {
      stream.end((error) => error ? reject(error) : resolve());
      stream.on("error", reject);
    });
    const actual = sha256(archivePath);
    if (actual !== expected) {
      throw new Error(`SDK SHA-256 mismatch: expected ${expected}, received ${actual}`);
    }
    return {
      archivePath,
      temporaryDirectory,
      assetName: asset.name,
      releaseTag: String(release.tag_name || release.name || "latest"),
      expectedDigest: expected,
      bytes: fs.statSync(archivePath).size
    };
  } catch (error) {
    fs.rmSync(temporaryDirectory, { recursive: true, force: true });
    throw error;
  }
}

function sdkInstallName(assetName) {
  return path.basename(assetName, ".zip");
}

module.exports = {
  REPOSITORY,
  SDK_ASSET_PATTERN,
  parseDigest,
  parseSidecar,
  sdkInstallName,
  selectSdkAsset,
  selectSdkRelease,
  downloadLatestSdk
};
