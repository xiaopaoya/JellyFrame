const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const vm = require('vm');
const cp = require('child_process');
const { configureConnection, connectionFile, readConnection } = require('../../tools/vscode-jellyframe/device_provider_connection');
const { isInside } = require('../../tools/vscode-jellyframe/author_environment');
const extension = path.resolve(__dirname, '../../tools/vscode-jellyframe');
const source = fs.readFileSync(path.join(extension, 'extension.js'), 'utf8');

async function run(root) {
  const directory = path.join(root, 'package', 'provider');
  fs.mkdirSync(directory, { recursive: true });
  const provider = path.join(directory, 'jellyframe-device.cmd');
  for (const name of ['jellyframe-device.cmd', 'jellyframe_device.py', 'jellyframe-device.config.example.json']) {
    fs.writeFileSync(path.join(directory, name), '');
  }
  const manifest = path.join(root, 'manifest.json');
  fs.writeFileSync(manifest, '{}');
  const filename = connectionFile(provider, {});
  let prompts = 0;
  let answer;
  let repair = false;
  const errors = [];
  const options = {
    manifest, environment: {},
    window: {
      showInputBox: async (input) => {
        prompts++;
        assert(input.validateInput('COMx'));
        assert(input.validateInput('COM0'));
        assert.equal(input.validateInput('com12'), undefined);
        return answer;
      },
      showWarningMessage: async (_, modal, action) => { assert(modal.modal); return repair ? action : undefined; },
      showInformationMessage() {},
      showErrorMessage: (message) => errors.push(message)
    }
  };
  assert.equal(await configureConnection(provider, options), false);
  assert(!fs.existsSync(filename), 'Cancel must not create config');
  answer = ' com12 ';
  assert.equal(await configureConnection(provider, options), true);
  assert.deepEqual(readConnection(filename), { endpointId: 'ws147-developer-local', port: 'COM12', baud: 115200, manifest });
  const saved = fs.readFileSync(filename, 'utf8');
  const before = prompts;
  assert.equal(await configureConnection(provider, options), true);
  assert.equal(prompts, before, 'Discovery must reuse valid connection');
  answer = undefined;
  assert.equal(await configureConnection(provider, { ...options, edit: true }), false);
  assert.equal(fs.readFileSync(filename, 'utf8'), saved);
  const custom = { endpointId: 'custom-device', port: 'COM42', baud: 230400, manifest: '../../manifest.json' };
  fs.writeFileSync(filename, JSON.stringify(custom));
  answer = 'COM7';
  assert.equal(await configureConnection(provider, { ...options, edit: true }), true);
  assert.deepEqual(readConnection(filename), { ...custom, port: 'COM7' });
  assert(fs.readdirSync(directory).some((name) => name.endsWith('.bak')));
  fs.writeFileSync(filename, 'invalid user file');
  assert.equal(await configureConnection(provider, options), false);
  assert.equal(fs.readFileSync(filename, 'utf8'), 'invalid user file');
  repair = true;
  assert.equal(await configureConnection(provider, options), true);
  assert(fs.readdirSync(directory).filter((name) => name.endsWith('.bak'))
    .some((name) => fs.readFileSync(path.join(directory, name), 'utf8') === 'invalid user file'));
  const adjacent = fs.readFileSync(filename, 'utf8');
  const overridden = path.join(root, 'override.json');
  assert.equal(await configureConnection(provider, { ...options, environment: { JELLYFRAME_DEVICE_CONFIG: overridden } }), true);
  assert.equal(readConnection(overridden).port, 'COM7');
  assert.equal(fs.readFileSync(filename, 'utf8'), adjacent);
  assert.equal(await configureConnection(provider, { ...options, environment: { JELLYFRAME_DEVICE_CONFIG: 'relative.json' } }), false);
  assert.equal(errors.length, 1);
  assert.equal(await configureConnection(path.join(root, 'third-party.exe'), options), true);
  assert.equal(await configureConnection(path.join(root, 'jellyframe-device.cmd'), options), true);

  // Discovery must not invoke the CLI when setup is cancelled.
  const discover = source.slice(source.indexOf('async function discoverDevice('), source.indexOf('function setSelectedDevice('));
  await vm.runInNewContext(`${discover}; discoverDevice`, {
    configuredDeviceProvider: () => provider, configureProviderConnection: async () => false,
    deviceCliArguments: () => assert.fail('Must not run discovery after cancellation')
  })({});

  const updates = [];
  const configure = source.slice(source.indexOf('async function configureProviderConnection('), source.indexOf('async function updateDeviceSetting('));
  await vm.runInNewContext(`${configure}; configureProviderConnection`, {
    config: () => ({ get: () => path.join(root, 'deleted-staging', 'manifest.json') }),
    deviceManifestPath: (_, value) => value, deviceProviderManifestCandidates: () => [manifest],
    fs, isChinese: () => false, vscode: { window: {} }, ensureOutputChannel: () => ({ appendLine() {} }),
    updateDeviceSetting: async (key, value) => { assert.equal(key, 'deviceManifest'); assert.equal(value, manifest); },
    configureConnection: async (_, options) => { assert.equal(options.manifest, manifest); return true; }
  })({}, provider);
  const update = source.slice(source.indexOf('async function updateDeviceSetting('), source.indexOf('async function configureDeviceProvider('));
  for (const [inspection, expected] of [[{}, 1], [{ workspaceValue: 'old' }, 2], [{ workspaceFolderValue: 'old' }, 3]]) {
    await vm.runInNewContext(`${update}; updateDeviceSetting`, {
      config: () => ({ inspect: () => inspection, update: (...args) => updates.push(args) }),
      vscode: { ConfigurationTarget: { Global: 1, Workspace: 2, WorkspaceFolder: 3 } }
    })('deviceProvider', provider);
    assert.equal(updates.at(-1)[2], expected);
  }
  if (process.platform === 'win32') await installTest(root);
}

async function installTest(root) {
  const staging = path.join(root, 'zip-source', 'test-provider');
  fs.mkdirSync(path.join(staging, 'provider'), { recursive: true });
  fs.mkdirSync(path.join(staging, 'developer-image'));
  fs.writeFileSync(path.join(staging, 'provider', 'jellyframe-device.cmd'), '');
  fs.writeFileSync(path.join(staging, 'developer-image', 'test.manifest.json'), '{}');
  const archive = path.join(root, 'test-provider.zip');
  const zipped = cp.spawnSync('powershell', ['-NoProfile', '-Command',
    'Compress-Archive -LiteralPath $env:JF_TEST_SOURCE -DestinationPath $env:JF_TEST_ZIP'], {
    env: { ...process.env, JF_TEST_SOURCE: staging, JF_TEST_ZIP: archive }, encoding: 'utf8'
  });
  assert.equal(zipped.status, 0, zipped.stderr);
  const parent = path.join(root, 'installation');
  fs.mkdirSync(parent);
  const settings = {};
  const install = source.slice(source.indexOf('async function installDeviceProvider('), source.indexOf('function configuredDeviceProvider('));
  const result = await vm.runInNewContext(`${install}; installDeviceProvider`, {
    process, path, fs, isInside, isChinese: () => false, statusProvider: undefined,
    fetchProviderCatalog: async () => ({ entries: [{ id: 'test', assetName: 'test-provider.zip' }] }),
    downloadProvider: async () => ({ archivePath: archive }),
    updateDeviceSetting: async (key, value) => { settings[key] = value; },
    configureProviderConnection: async (_, provider) => {
      assert(fs.statSync(provider).isFile());
      assert(fs.statSync(settings.deviceManifest).isFile());
      assert(!settings.deviceManifest.includes('.jellyframe-provider-install-'));
      return true;
    },
    runLocalTool: async (_, command, args) => {
      const result = cp.spawnSync(command, args, { encoding: 'utf8' });
      assert.equal(result.status, 0, result.stderr);
      return { code: result.status, stdout: result.stdout };
    },
    ensureOutputChannel: () => ({ appendLine() {} }),
    vscode: { ProgressLocation: { Notification: 15 }, window: {
      withProgress: (_, fn) => fn({ report() {} }), showQuickPick: async (choices) => choices[0],
      showOpenDialog: async () => [{ fsPath: parent }], showInformationMessage() {},
      showErrorMessage: (message) => assert.fail(message)
    } }
  })({ extensionPath: extension });
  assert.equal(result, path.join(parent, 'test-provider'));
  assert(fs.statSync(settings.deviceProvider).isFile());
  assert(fs.statSync(settings.deviceManifest).isFile(), 'Final manifest survives staging cleanup');
  assert.deepEqual(fs.readdirSync(parent), ['test-provider']);
}

const root = fs.mkdtempSync(path.join(os.tmpdir(), 'jf-provider-connection-'));
run(root).then(() => console.log('Provider connection and installation lifecycle tests passed'))
  .catch((error) => { console.error(error); process.exitCode = 1; })
  .finally(() => fs.rmSync(root, { recursive: true, force: true }));
