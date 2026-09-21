const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const path = require('path');
const { startupMessage, withStartupProgress } = require('../../tools/vscode-jellyframe/startup_feedback');

async function main() {
  assert.match(startupMessage(9999), /Loading \(9s\)/);
  assert.match(startupMessage(10000), /still waiting/);
  assert.match(startupMessage(30000, true), /30 秒/);
  const reports = [];
  let cleared = 0;
  const vscode = {
    ProgressLocation: { Notification: 15 },
    window: { withProgress(options, task) {
      assert.equal(options.cancellable, false);
      assert.equal(options.location, 15);
      return task({ report: (value) => reports.push(value) });
    } }
  };
  const timers = {
    setInterval(callback) { callback(); return 42; },
    clearInterval(value) { assert.equal(value, 42); cleared++; }
  };
  assert.equal(await withStartupProgress(vscode, 'Preview', async () => 7, false, timers), 7);
  await assert.rejects(withStartupProgress(vscode, 'Preview', async () => { throw Error('failed'); }, false, timers), /failed/);
  assert.equal(cleared, 2);
  assert(reports.length >= 4);

  const source = fs.readFileSync(path.join(__dirname, '../../tools/vscode-jellyframe/extension.js'), 'utf8');
  const htmlFunction = source.slice(source.indexOf('function embeddedDebugHtml('), source.indexOf('function stopEmbeddedDebugSession('));
  for (const chinese of [false, true]) {
    const html = vm.runInNewContext(`${htmlFunction}; embeddedDebugHtml({})`, {
      startupMessage, vscode: { env: { language: chinese ? 'zh-cn' : 'en' } }, Date, Math
    });
    new vm.Script(html.match(/<script nonce="[^"]+">([\s\S]*?)<\/script>/)[1]);
    assert(html.includes('#frame[hidden], #empty[hidden] { display: none; }'));
    assert(html.includes('stopLoading();'));
    assert(html.includes('renderToken += 1;'));
  }

  const deliver = source.slice(source.indexOf('async function deliverEmbeddedFrame('), source.indexOf('function validEmbeddedViewport('));
  const messages = [];
  const context = {
    fs: { promises: { readFile: async () => Buffer.from('bitmap') }, rm: () => {} },
    postEmbeddedMessage: (_, message) => messages.push(message),
    scheduleEmbeddedDiagnostics: () => {}, appendEmbeddedLog: () => {}
  };
  const deliverFrame = vm.runInNewContext(`${deliver}; deliverEmbeddedFrame`, context);
  const session = { runId: 1, active: true, latestAnnouncedSequence: 1, lastDeliveredSequence: 0, deliveredFrames: 0 };
  await deliverFrame(session, { path: 'frame.bmp', sequence: 1, width: 172, height: 320 });
  assert.equal(messages.length, 0);
  assert.equal(session.pendingFrame.sequence, 1);
  session.webviewReady = true;
  session.latestAnnouncedSequence = 2;
  await deliverFrame(session, { path: 'frame.bmp', sequence: 2, width: 172, height: 320 });
  assert.equal(messages.length, 1);
  assert.equal(messages[0].sequence, 2);
  console.log('Startup feedback tests passed');
}
main().catch((error) => { console.error(error); process.exitCode = 1; });
