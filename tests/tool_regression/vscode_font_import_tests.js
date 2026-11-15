"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const source = fs.readFileSync(path.join(__dirname, "../../tools/vscode-jellyframe/extension.js"), "utf8");
const begin = source.indexOf("function fontHasRedistributionMetadata(");
const end = source.indexOf("async function packageMissingFonts(", begin);
assert(begin >= 0 && end > begin);

async function choose(fonts, options = {}, selection = "C:\\Fonts\\licensed.jffont2") {
  const errors = [];
  const dialogs = [];
  let prompt = 0;
  const context = {
    missingManifestFonts: () => fonts,
    isChinese: () => false,
    packageManifestPath: () => "C:\\App\\jellyframe.app.json",
    vscode: {
      Uri: { file: value => value },
      commands: { executeCommand: async () => {} },
      window: {
        showInformationMessage: () => {},
        showErrorMessage: async message => { errors.push(message); },
        showQuickPick: async choices => {
          ++prompt;
          return choices[0];
        },
        showOpenDialog: async settings => {
          dialogs.push(settings);
          return selection ? [{ fsPath: selection }] : undefined;
        }
      }
    }
  };
  vm.createContext(context);
  vm.runInContext(source.slice(begin, end), context);
  const result = await context.selectMissingFontImport("C:\\App", options);
  return { result: result && Array.from(result), errors, dialogs, prompt };
}

async function main() {
  const font = { id: "ui", source: "/fonts/ui.jffont2", license: { name: "OFL-1.1", source: "Licensed face" } };
  assert.deepStrictEqual((await choose([])).result, []);
  const valid = await choose([font], { required: true });
  assert.deepStrictEqual(valid.result, ["--import-font-resource", "/fonts/ui.jffont2=C:\\Fonts\\licensed.jffont2"]);
  assert.deepStrictEqual(Array.from(valid.dialogs[0].filters["Core prepared font"]), ["jffont2"]);
  assert.equal(valid.prompt, 0);
  assert.equal((await choose([font])).prompt, 1);
  assert.equal((await choose([font], { required: true }, "")).result, undefined);
  const old = await choose([{ ...font, source: "/fonts/ui.jffont" }], { required: true });
  assert.equal(old.result, undefined);
  assert.equal(old.dialogs.length, 0);
  assert.match(old.errors[0], /\.jffont2 source/);
  const unlicensed = await choose([{ ...font, license: {} }], { required: true });
  assert.equal(unlicensed.result, undefined);
  assert.equal(unlicensed.dialogs.length, 0);
  assert.match(unlicensed.errors[0], /license/);
}

main().catch(error => { console.error(error); process.exitCode = 1; });
