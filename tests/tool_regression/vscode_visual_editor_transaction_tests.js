"use strict";

const assert = require("assert");
const Module = require("module");

const originalLoad = Module._load;
Module._load = function mockVscode(request, parent, isMain) {
  if (request === "vscode") return { env: { language: "en" } };
  return originalLoad.call(this, request, parent, isMain);
};
const { replaceDocuments } = require("../../tools/vscode-jellyframe/visual_editor");
Module._load = originalLoad;

function harness(savePlans, rollbackApply = true) {
  const states = savePlans.map((plan, index) => ({
    filename: `file-${index}.txt`,
    text: `original-${index}`,
    saves: [...plan],
    saveCount: 0
  }));
  const byPath = new Map(states.map((state) => [state.filename, state]));
  let applyCount = 0;

  class FakeEdit {
    constructor() {
      this.changes = [];
    }

    replace(uri, range, text) {
      this.changes.push({ uri, text });
    }
  }

  const documents = states.map((state) => ({
    uri: { fsPath: state.filename },
    get lineCount() { return 1; },
    lineAt() { return { range: { end: { line: 0, character: state.text.length } } }; },
    getText() { return state.text; },
    async save() {
      state.saveCount += 1;
      const result = state.saves.shift();
      if (result) state.text = state.text;
      return result === undefined ? true : result;
    }
  }));

  const api = {
    Uri: { file: (filename) => ({ fsPath: filename }) },
    Position: class Position {},
    Range: class Range {},
    WorkspaceEdit: FakeEdit,
    workspace: {
      async openTextDocument(uri) { return documents[states.indexOf(byPath.get(uri.fsPath))]; },
      async applyEdit(edit) {
        applyCount += 1;
        if (applyCount === 2 && !rollbackApply) return false;
        for (const change of edit.changes) byPath.get(change.uri.fsPath).text = change.text;
        return true;
      }
    }
  };
  return { api, states };
}

(async () => {
  const entries = [["file-0.txt", "updated-0"], ["file-1.txt", "updated-1"], ["file-2.txt", "updated-2"]];
  const successfulRollback = harness([[true, true], [false, true], [true, true]]);
  await assert.rejects(
    () => replaceDocuments(entries, successfulRollback.api),
    /source write failed and was rolled back/
  );
  assert.deepEqual(successfulRollback.states.map((state) => state.text), ["original-0", "original-1", "original-2"]);
  assert.deepEqual(successfulRollback.states.map((state) => state.saveCount), [2, 2, 1]);

  const failedRollback = harness([[true, true], [false, false], [true, true]], false);
  await assert.rejects(
    () => replaceDocuments(entries, failedRollback.api),
    /source write failed:.*rollback failed/
  );

  const success = harness([[true], [true], [true]]);
  await replaceDocuments(entries, success.api);
  assert.deepEqual(success.states.map((state) => state.text), ["updated-0", "updated-1", "updated-2"]);
  console.log("VS Code visual-editor transaction tests passed");
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
