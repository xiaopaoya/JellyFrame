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

module.exports = {
  deviceChoice,
  deviceSummary,
  discoverySummary,
  identitySummary,
  advertisedDeviceOperations,
  deviceSupportsOperation
};
