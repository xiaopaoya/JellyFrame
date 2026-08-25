"use strict";

const DEFAULT_CAPTURE_LIMIT = 1024 * 1024;

function appendBoundedOutput(current, chunk, limit = DEFAULT_CAPTURE_LIMIT) {
  const previous = typeof current === "string" ? current : "";
  const incoming = typeof chunk === "string" ? chunk : String(chunk || "");
  const maximum = Number.isSafeInteger(limit) && limit > 0 ? limit : DEFAULT_CAPTURE_LIMIT;
  const available = Math.max(0, maximum - Buffer.byteLength(previous, "utf8"));
  if (available === 0) {
    return { text: previous, appended: "", truncated: incoming.length > 0 };
  }
  const bytes = Buffer.from(incoming, "utf8");
  if (bytes.length <= available) {
    return { text: previous + incoming, appended: incoming, truncated: false };
  }
  const appended = bytes.subarray(0, available).toString("utf8");
  return { text: previous + appended, appended, truncated: true };
}

function parseStructuredResult(stdout) {
  const text = typeof stdout === "string" ? stdout.trim() : "";
  if (!text) {
    return undefined;
  }
  try {
    const result = JSON.parse(text);
    if (Array.isArray(result)) {
      const terminal = result[result.length - 1];
      return terminal && typeof terminal === "object" && !Array.isArray(terminal) ? terminal : undefined;
    }
    return result && typeof result === "object" ? result : undefined;
  } catch (_) {
    for (const line of text.split(/\r?\n/).reverse()) {
      try {
        const result = JSON.parse(line);
        if (result && typeof result === "object" && !Array.isArray(result)) {
          return result;
        }
      } catch (_) {
        // A tool may emit ordinary text before its structured terminal result.
      }
    }
  }
  return undefined;
}

function conciseText(value, maximum = 360) {
  const text = String(value || "").replace(/\s+/g, " ").trim();
  if (!text) {
    return "";
  }
  return text.length > maximum ? `${text.slice(0, maximum - 1)}…` : text;
}

function providerReason(resultCode, chinese) {
  const reasons = chinese ? {
    "invalid-request": "请求无效。请检查输入、Provider 配置和设备选择。",
    "transport-unavailable": "无法连接配置的设备。请关闭其他串口监视器并检查 USB 连接。",
    "protocol-mismatch": "设备协议或 Developer Image 不匹配。请核对 Provider 与 manifest。",
    "provider-failed": "Provider 内部执行失败。请查看 JellyFrame 运行日志。",
    busy: "Provider 正忙。请等待当前设备操作结束后重试。",
    unsupported: "当前 Provider 不支持该操作。",
    denied: "设备拒绝了该请求。",
    "not-found": "未找到请求的设备或资源。",
    "stale-session": "设备会话已过期。请先重新发现设备。",
    "stale-request": "设备拒绝了过期请求。请先重新发现设备。",
    "payload-too-large": "请求数据超过设备允许的上限。",
    "integrity-failed": "设备拒绝了未通过完整性校验的数据。",
    "storage-full": "设备可用存储空间不足。",
    cancelled: "操作已取消。",
    failed: "设备未能完成操作。"
  } : {
    "invalid-request": "The request is invalid. Check the input, provider configuration, and device selection.",
    "transport-unavailable": "The configured device is unavailable. Close other serial monitors and check the USB connection.",
    "protocol-mismatch": "The device protocol or Developer Image does not match. Check the provider and manifest.",
    "provider-failed": "The provider failed internally. Open the JellyFrame run log for details.",
    busy: "The provider is busy. Wait for the current device operation to finish, then retry.",
    unsupported: "The configured provider does not support this operation.",
    denied: "The device denied this request.",
    "not-found": "The requested device or resource was not found.",
    "stale-session": "The device session is stale. Discover the device again first.",
    "stale-request": "The device rejected a stale request. Discover the device again first.",
    "payload-too-large": "The request exceeds the device data limit.",
    "integrity-failed": "The device rejected data that failed integrity validation.",
    "storage-full": "The device does not have enough available storage.",
    cancelled: "The operation was cancelled.",
    failed: "The device did not complete the operation."
  };
  return reasons[resultCode] || (chinese ? "设备返回了未完成结果。" : "The device returned an incomplete result.");
}

function commandFailure({ operation, stdout, stderr, chinese = false, internalError } = {}) {
  const label = conciseText(operation) || (chinese ? "JellyFrame 命令" : "JellyFrame command");
  const structured = parseStructuredResult(stdout);
  if (internalError) {
    const detail = conciseText(internalError);
    return {
      resultCode: "internal-error",
      message: chinese
        ? `${label}失败：扩展无法处理工具返回结果。${detail ? ` ${detail}` : " 请查看 JellyFrame 运行日志。"}`
        : `${label} failed: the extension could not process the tool result.${detail ? ` ${detail}` : " Open the JellyFrame run log."}`
    };
  }
  if (structured && typeof structured.resultCode === "string") {
    const detail = conciseText(structured.message);
    const reason = providerReason(structured.resultCode, chinese);
    return {
      resultCode: structured.resultCode,
      message: `${label}${chinese ? "失败" : " failed"}：${reason}${detail ? ` ${detail}` : ""}`
    };
  }
  const diagnostic = conciseText(stderr);
  return {
    resultCode: "tool-error",
    message: diagnostic
      ? `${label}${chinese ? "失败：" : " failed: "}${diagnostic}`
      : (chinese
        ? `${label}失败：工具没有返回可读诊断。请查看 JellyFrame 运行日志。`
        : `${label} failed: the tool did not return a readable diagnostic. Open the JellyFrame run log.`)
  };
}

module.exports = {
  DEFAULT_CAPTURE_LIMIT,
  appendBoundedOutput,
  commandFailure,
  parseStructuredResult
};
