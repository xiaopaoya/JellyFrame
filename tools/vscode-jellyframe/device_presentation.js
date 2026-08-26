"use strict";

function nonEmpty(value, fallback = "?") {
  return typeof value === "string" && value.trim() ? value.trim() : fallback;
}

function deviceChoice(device, chinese) {
  const connected = device?.connected ? (chinese ? "已连接" : "connected") : (chinese ? "未连接" : "disconnected");
  return {
    label: nonEmpty(device?.endpointId),
    description: `${nonEmpty(device?.boardId, "unknown")} ${nonEmpty(device?.profileId, "unknown")}`,
    detail: `${nonEmpty(device?.imageVersion, "unknown image")} · ${connected}`,
    endpointId: device?.endpointId
  };
}

function deviceSummary(device, chinese) {
  const prefix = nonEmpty(device?.endpointId);
  const board = nonEmpty(device?.boardId, "unknown");
  const profile = nonEmpty(device?.profileId, "unknown");
  const image = nonEmpty(device?.imageVersion, "unknown image");
  const connected = device?.connected ? (chinese ? "已连接" : "connected") : (chinese ? "未连接" : "disconnected");
  return `${prefix} · ${board} · ${profile} · ${image} · ${connected}`;
}

function identitySummary(device, identity, chinese) {
  const core = nonEmpty(identity?.renderCoreVersion);
  const abi = Number.isInteger(identity?.renderCoreAbi) ? identity.renderCoreAbi : "?";
  return `${deviceSummary(device, chinese)} · ${chinese ? "Render Core" : "Render Core"} ${core} / ABI ${abi}`;
}

function discoverySummary(devices, chinese) {
  const total = Array.isArray(devices) ? devices.length : 0;
  const connected = Array.isArray(devices) ? devices.filter((device) => device?.connected).length : 0;
  return chinese
    ? `发现 ${total} 个设备，其中 ${connected} 个已连接。`
    : `Discovered ${total} device(s), ${connected} connected.`;
}

const DEVICE_LIFECYCLE_OPERATIONS = new Set([
  "install", "cancel", "launch", "stop", "remove", "rollback", "logs", "recovery"
]);

function advertisedDeviceOperations(device) {
  const operations = device?.capabilities?.supportedOperations;
  if (!Array.isArray(operations)) {
    return new Set();
  }
  return new Set(operations.filter((operation) => DEVICE_LIFECYCLE_OPERATIONS.has(operation)));
}

function deviceSupportsOperation(device, operation) {
  return advertisedDeviceOperations(device).has(operation);
}

function positiveInteger(value) {
  return Number.isInteger(value) && value > 0;
}

// Deployment must be bound to exactly one target matching the attested display.
// Target ids may be product-specific, but a same-size ambiguity must be rejected
// before packaging or opening a device install transaction.
function matchingDeviceTarget(manifest, device) {
  const targets = manifest?.targets;
  const display = device?.capabilities?.display;
  if (!targets || typeof targets !== "object" || Array.isArray(targets)
      || !positiveInteger(display?.width) || !positiveInteger(display?.height)) {
    return undefined;
  }
  const matchesDisplay = (target) => target && typeof target === "object"
    && !Array.isArray(target) && target.viewport && typeof target.viewport === "object"
    && Number(target.viewport.width) === display.width
    && Number(target.viewport.height) === display.height;
  const matches = Object.entries(targets)
    .filter(([, target]) => matchesDisplay(target))
    .map(([id]) => id);
  return matches.length === 1 ? matches[0] : undefined;
}

module.exports = {
  deviceChoice,
  deviceSummary,
  discoverySummary,
  identitySummary,
  advertisedDeviceOperations,
  deviceSupportsOperation,
  matchingDeviceTarget
};
