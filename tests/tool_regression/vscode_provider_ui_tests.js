const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const source = fs.readFileSync(path.join(__dirname, '../../tools/vscode-jellyframe/extension.js'), 'utf8');

async function testPicker() {
  const installSource = source.slice(source.indexOf('async function installDeviceProvider('),
    source.indexOf('function configuredDeviceProvider('));
  for (const count of [0, 1, 2]) {
    for (const cancel of [true, false]) {
      const calls = [];
      const entries = Array.from({ length: count }, (_, index) => ({
        id: `board-${index}`, name: `Board ${index}`, board: `board-${index}`,
        imageVersion: '1.0', renderCoreVersion: '0.6.2', platforms: ['win32-x64']
      }));
      const install = vm.runInNewContext(`${installSource}; installDeviceProvider`, {
        process: { platform: 'win32' }, isChinese: () => false,
        fetchProviderCatalog: async () => ({ entries }),
        downloadProvider: () => assert.fail('Must not download before choosing a folder'),
        vscode: {
          ProgressLocation: { Notification: 15 },
          window: {
            withProgress: (_, run) => run(),
            showInformationMessage: () => calls.push('empty'),
            showQuickPick: async (choices, options) => {
              calls.push('pick');
              assert.equal(choices.length, count);
              assert.equal(options.matchOnDescription, true);
              assert(choices[0].description.includes('Core 0.6.2'));
              return cancel ? undefined : choices.at(-1);
            },
            showOpenDialog: async (options) => {
              calls.push('folder');
              assert(options.title.includes(`Board ${count - 1}`));
              return undefined;
            }
          }
        }
      });
      await install({});
      assert.deepEqual(calls, count === 0 ? ['empty'] : cancel ? ['pick'] : ['pick', 'folder']);
    }
  }
}

function testGroups() {
  const classSource = source.slice(source.indexOf('class JellyFrameStatusProvider'), source.indexOf('function escapeHtml('));
  for (const chinese of [false, true]) {
    for (const hasApp of [false, true]) {
      for (const mode of ['none', 'readonly', 'lifecycle']) {
        const connected = mode !== 'none';
        const device = connected ? {} : undefined;
        const context = {
          path, isChinese: () => chinese,
          currentPackageRoot: () => hasApp ? '/app' : undefined,
          missingManifestFonts: () => [], isVisualEditorEligible: () => true,
          updateVisualEditorContext: () => {}, appRequiresScripting: () => false,
          nativeBuildDir: () => ({ buildDirectory: '/build' }),
          workspaceFolderPath: () => '/app', resolveSdkRoot: () => undefined,
          config: () => ({ get: (_, fallback) => fallback }), readSdkMetadata: () => undefined,
          activeDesktopBuildSetup: undefined,
          desktopBuildPresentation: () => ({ summary: 'Runtime', profile: 'Release', output: '/build', scripting: 'off' }),
          lastReport: undefined, lastCapturePath: undefined, lastTracePath: undefined,
          selectedDeviceRecord: () => device,
          advertisedDeviceOperations: () => new Set(mode === 'lifecycle' ? ['install', 'launch', 'stop', 'logs'] : []),
          lastDeviceDiscovery: undefined, lastDeviceEndpoint: connected ? 'device' : undefined,
          lastDeviceInfo: undefined, lastDeviceApps: connected ? { apps: [{ appId: 'example' }] } : undefined,
          activeDeviceOperation: undefined, lastDeviceFailure: undefined, lastDeviceLifecycle: undefined,
          vscode: {
            env: { language: chinese ? 'zh-cn' : 'en' },
            EventEmitter: class { fire() {} },
            TreeItem: class { constructor(label, collapsibleState) { Object.assign(this, { label, collapsibleState }); } },
            ThemeIcon: class {}, TreeItemCollapsibleState: { None: 0, Expanded: 2 },
            Uri: { file: (value) => value }
          }
        };
        const Provider = vm.runInNewContext(`${classSource}; JellyFrameStatusProvider`, context);
        const provider = new Provider({ extensionPath: '/extension' });
        const groups = provider.getChildren();
        const commands = new Map();
        for (const group of groups) {
          assert(group.children.length > 0, 'No empty sections');
          for (const item of group.children) {
            assert(!item.children, 'No second-level groups');
            assert.equal(item.collapsibleState, 0);
            assert.equal(provider.getParent(item), group);
            if (item.command) {
              assert(!commands.has(item.command.command), 'Command appears only once');
              commands.set(item.command.command, { group, item });
            }
          }
        }
        const install = commands.get('jellyframe.deviceInstallProvider');
        assert.equal(install.item.label, chinese ? '\u5b89\u88c5 Provider' : 'Install Provider');
        assert(install.item.description.includes('Device Provider'));
        assert.equal(install.item.tooltip, install.item.description);
        assert.equal(install.group.label, chinese ? '\u8bbe\u5907\u8fde\u63a5' : 'Device Connection');
        assert.equal(commands.has('jellyframe.deviceList'), connected);
        assert.equal(commands.has('jellyframe.deviceLaunch'), mode === 'lifecycle');
        assert.equal(commands.has('jellyframe.deviceDeploy'), hasApp && mode === 'lifecycle');
        assert.equal(commands.has('jellyframe.debug'), hasApp);
        if (hasApp) {
          const check = commands.get('jellyframe.check').group;
          const debug = commands.get('jellyframe.debug').group;
          const create = commands.get('jellyframe.newFromTemplate').group;
          assert.notEqual(check, debug);
          assert.notEqual(create, check);
          assert.notEqual(create, debug);
        }
      }
    }
  }
}

testPicker().then(() => {
  testGroups();
  console.log('Provider picker and flat activity groups tests passed');
}).catch((error) => { console.error(error); process.exitCode = 1; });
