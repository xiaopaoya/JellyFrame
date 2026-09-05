"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");

const extension = fs.readFileSync(path.join(__dirname, "../../tools/vscode-jellyframe/extension.js"), "utf8");
const visualEditor = fs.readFileSync(path.join(__dirname, "../../tools/vscode-jellyframe/visual_editor.js"), "utf8");
const diagnostics = fs.readFileSync(path.join(__dirname, "../../tools/vscode-jellyframe/visual_editor_diagnostics.js"), "utf8");

assert(visualEditor.includes('requestedViewport: model.viewport'), "save-and-run must forward the visual model viewport");
assert(extension.includes('debugApp(context, resourceUri, options)'), "debug command must accept handoff options");
assert(extension.includes("requestedEmbeddedViewport(options)"), "debug session must validate the requested viewport");
assert(extension.includes("'--runtime-log', session.runtimeLog"), "embedded sessions must isolate runtime logs");
assert(extension.includes("runEmbeddedDebugReport(context, session)"), "embedded session close must trigger the existing check report flow");
assert(visualEditor.includes('visualEditorPanel: panel'), "visual editor must register the runtime state bridge");
assert(visualEditor.includes('id=\"runtime-state\"'), "visual editor must expose runtime state");
assert(extension.includes("postVisualEditorRuntime(session, 'reporting')"), "report generation state must reach the visual editor");
assert(visualEditor.includes('message?.type === "model-check"'), "visual editor must expose bounded model checks");
assert(diagnostics.includes("stable-node-id"), "pipeline diagnostics must have explicit stable-node attribution");

console.log("VS Code Stage 3 handoff tests passed");
