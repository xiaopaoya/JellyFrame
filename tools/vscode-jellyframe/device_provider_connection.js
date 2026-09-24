const fs = require("fs");
const os = require("os");
const path = require("path");

function expandHome(value) {
  return /^~([\\/]|$)/.test(value) ? path.join(os.homedir(), value.slice(2)) : value;
}

function connectionFile(provider, environment = process.env) {
  const directory = path.dirname(provider);
  // Only the bundled serial provider uses this configuration contract.
  if (path.basename(provider).toLowerCase() !== "jellyframe-device.cmd" ||
      !fs.existsSync(path.join(directory, "jellyframe_device.py")) ||
      !fs.existsSync(path.join(directory, "jellyframe-device.config.example.json"))) {
    return undefined;
  }
  const override = environment.JELLYFRAME_DEVICE_CONFIG;
  if (override && !path.isAbsolute(expandHome(override))) {
    throw new Error("JELLYFRAME_DEVICE_CONFIG must be an absolute path; restart VS Code after changing it.");
  }
  return override ? expandHome(override) : path.join(directory, "jellyframe-device.config.json");
}

function readConnection(filename) {
  const value = JSON.parse(fs.readFileSync(filename, "utf8"));
  if (!value || Object.keys(value).sort().join(",") !== "baud,endpointId,manifest,port" ||
      typeof value.endpointId !== "string" || !/^[\x00-\x7f]{1,96}$/.test(value.endpointId) ||
      typeof value.port !== "string" || !value.port.trim() || /^COMx$/i.test(value.port) ||
      !Number.isInteger(value.baud) || value.baud < 9600 || value.baud > 1000000 ||
      typeof value.manifest !== "string" || !value.manifest) {
    throw new Error("Invalid serial provider configuration");
  }
  const manifest = path.resolve(path.dirname(filename), expandHome(value.manifest));
  JSON.parse(fs.readFileSync(manifest, "utf8"));
  return value;
}

async function configureConnection(provider, options) {
  const { window, chinese = false, edit = false, manifest, log = () => {} } = options;
  const say = (zh, en) => chinese ? zh : en;
  let filename;
  try {
    filename = connectionFile(provider, options.environment);
    if (!filename) return true;
    let existing;
    try { existing = readConnection(filename); } catch (_) { /* Missing or invalid config needs user input. */ }
    if (existing && !edit) return true;

    const exists = fs.existsSync(filename);
    if (exists && !existing) {
      const repair = say("备份并重新配置", "Back up and reconfigure");
      const choice = await window.showWarningMessage(say(
        `连接配置无效：${filename}。重新配置前会保留备份。`,
        `Invalid connection configuration: ${filename}. A backup will be kept before replacing it.`),
      { modal: true }, repair);
      if (choice !== repair) return false;
    }
    const port = await window.showInputBox({
      title: say("配置设备串口", "Configure device serial port"),
      prompt: say("填写当前 Windows 设备管理器中的 COM 端口；虚拟机需先连接 USB 设备。不会自动扫描或打开串口。",
        "Enter the COM port shown in this Windows Device Manager. Attach USB to the VM first. No ports are scanned or opened."),
      value: existing?.port || "",
      placeHolder: "COM3",
      ignoreFocusOut: true,
      validateInput: (value) => /^COM[1-9][0-9]*$/i.test(value.trim()) ? undefined : say("请输入有效端口，例如 COM3", "Enter a valid port, for example COM3")
    });
    if (port === undefined) {
      window.showInformationMessage(existing ? say("已取消，保留原有连接配置。", "Cancelled; existing connection settings kept.")
        : say("Provider 已安装；串口配置未完成。可再次点击配置 Provider 或发现设备。",
          "Provider is installed; serial setup is incomplete. Run Configure Provider or Discover Device to continue."));
      return false;
    }
    if (!/^COM[1-9][0-9]*$/i.test(port.trim())) return false;
    const value = existing ? { ...existing, port: port.trim().toUpperCase() } : {
      endpointId: "ws147-developer-local", port: port.trim().toUpperCase(), baud: 115200,
      manifest: manifest ? path.resolve(manifest) : ""
    };
    if (!value.manifest) throw new Error("Select a valid Developer Image manifest first.");
    JSON.parse(fs.readFileSync(path.resolve(path.dirname(filename), expandHome(value.manifest)), "utf8"));
    if (exists) {
      const backup = `${filename}.${Date.now()}.bak`;
      fs.copyFileSync(filename, backup, fs.constants.COPYFILE_EXCL);
      log(`Provider connection backup: ${backup}`);
    }
    // Exclusive creation protects missing-config setup from overwriting a concurrent edit.
    fs.writeFileSync(filename, `${JSON.stringify(value, null, 2)}\n`, { encoding: "utf8", flag: exists ? "w" : "wx" });
    log(`Provider connection configured: ${filename} (${value.port}, ${value.baud})`);
    return true;
  } catch (error) {
    const message = say(`设备连接配置失败：${error.message}`, `Device connection configuration failed: ${error.message}`);
    log(`[error] ${message}`);
    window.showErrorMessage(message);
    return false;
  }
}

module.exports = { connectionFile, readConnection, configureConnection };
