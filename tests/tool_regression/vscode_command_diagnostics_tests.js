const assert = require("assert");
const {
  DEFAULT_CAPTURE_LIMIT,
  appendBoundedOutput,
  commandFailure,
  protocolMismatchReason,
  parseStructuredResult
} = require("../../tools/vscode-jellyframe/command_diagnostics");

function main() {
  const full = appendBoundedOutput("", "abcdef", 4);
  assert.equal(full.text, "abcd");
  assert.equal(full.appended, "abcd");
  assert.equal(full.truncated, true);
  assert.equal(appendBoundedOutput(full.text, "z", 4).appended, "");
  assert.equal(DEFAULT_CAPTURE_LIMIT, 1024 * 1024);

  const result = parseStructuredResult("notice\n{\"resultCode\":\"transport-unavailable\",\"message\":\"COM19 is busy\"}\n");
  assert.equal(result.resultCode, "transport-unavailable");
  assert.equal(result.message, "COM19 is busy");

  const streamed = parseStructuredResult(JSON.stringify([
    { kind: "progress", operation: "install", resultCode: "accepted" },
    { kind: "result", operation: "install", resultCode: "integrity-failed", message: "bundle CRC mismatch" }
  ]));
  assert.equal(streamed.resultCode, "integrity-failed");
  assert.equal(streamed.message, "bundle CRC mismatch");

  const providerFailure = commandFailure({
    operation: "发现设备",
    chinese: true,
    stdout: JSON.stringify({ resultCode: "transport-unavailable", message: "configured endpoint is unavailable" })
  });
  assert.equal(providerFailure.resultCode, "transport-unavailable");
  assert.match(providerFailure.message, /关闭其他串口监视器/);
  assert.match(providerFailure.message, /configured endpoint is unavailable/);
  assert.doesNotMatch(providerFailure.message, /code 3/);

  const identityMismatch = commandFailure({
    operation: "发现设备",
    chinese: true,
    stdout: JSON.stringify({
      resultCode: "protocol-mismatch",
      message: "Developer Image identity mismatch at sourceRevision: manifest expects '9e32face'; device attests '84611819'. Install the Provider and manifest paired with this firmware, or flash the Developer Image described by the configured manifest."
    })
  });
  assert.match(identityMismatch.message, /镜像源版本不匹配/);
  assert.match(identityMismatch.message, /9e32face/);
  assert.match(identityMismatch.message, /84611819/);
  assert.match(identityMismatch.message, /刷写当前 manifest 对应的 Developer Image/);
  assert.match(protocolMismatchReason("unrecognized protocol response", true), /设备协议/);

  const parserFailure = commandFailure({ operation: "Discover device", stderr: "usage: jellyframe_cli.py device ..." });
  assert.match(parserFailure.message, /usage: jellyframe_cli.py device/);
  assert.doesNotMatch(parserFailure.message, /code 2/);

  const fallback = commandFailure({ operation: "发现设备", chinese: true });
  assert.match(fallback.message, /没有返回可读诊断/);
}

main();
