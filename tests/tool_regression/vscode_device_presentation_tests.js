const assert = require("assert");
const {
  deviceChoice,
  deviceSummary,
  discoverySummary,
  identitySummary,
  advertisedDeviceOperations,
  deviceSupportsOperation,
  matchingDeviceTarget
} = require("../../tools/vscode-jellyframe/device_presentation");

function device(endpointId, connected = true) {
  return {
    endpointId,
    boardId: "ws147",
    profileId: "rect-172x320",
    imageVersion: "0.6.0-a2",
    connected
  };
}

function main() {
  const choice = deviceChoice(device("desk-a"), true);
  assert.deepEqual(choice, {
    label: "desk-a",
    description: "ws147 rect-172x320",
    detail: "0.6.0-a2 · 已连接",
    endpointId: "desk-a"
  });
  assert.match(deviceSummary(device("desk-a"), true), /desk-a · ws147 · rect-172x320 · 0\.6\.0-a2 · 已连接/);
  assert.equal(discoverySummary([device("desk-a"), device("desk-b", false)], false), "Discovered 2 device(s), 1 connected.");
  assert.match(identitySummary(device("desk-a"), { renderCoreVersion: "0.6.1", renderCoreAbi: 1 }, true), /Render Core 0\.6\.1 \/ ABI 1/);
  const lifecycleDevice = device("desk-lifecycle");
  lifecycleDevice.capabilities = { supportedOperations: ["install", "launch", "logs"] };
  assert.deepEqual([...advertisedDeviceOperations(lifecycleDevice)].sort(), ["install", "launch", "logs"]);
  assert.equal(deviceSupportsOperation(lifecycleDevice, "install"), true);
  assert.equal(deviceSupportsOperation(device("read-only"), "install"), false);
  const targetDevice = device("desk-target");
  targetDevice.capabilities = { display: { width: 172, height: 320, shape: "rect" } };
  assert.equal(matchingDeviceTarget({ targets: {
    "rect-172x320": { viewport: { width: 172, height: 320 } },
    "round-300": { viewport: { width: 300, height: 300 } }
  } }, targetDevice), "rect-172x320");
  targetDevice.profileId = "vendor-wearable";
  assert.equal(matchingDeviceTarget({ targets: {
    "vendor-wearable": { viewport: { width: 172, height: 320 } },
    "other": { viewport: { width: 172, height: 320 } }
  } }, targetDevice), "vendor-wearable");
  assert.equal(matchingDeviceTarget({ targets: {
    "a": { viewport: { width: 172, height: 320 } },
    "b": { viewport: { width: 172, height: 320 } }
  } }, targetDevice), undefined);
}

main();
