const fs = require("fs");
const path = require("path");
const childProcess = require("child_process");
const vscode = require("vscode");
const { selectBuildDirectory } = require("./build_profiles");
const {
  desktopBuildPlan,
  jerryscriptBuildArguments,
  jerryscriptState,
  managedProfileRoot
} = require("./desktop_build_setup");
const { appendBoundedOutput, commandFailure } = require("./command_diagnostics");
const {
  deviceChoice,
  deviceSummary,
  discoverySummary,
  identitySummary,
  advertisedDeviceOperations,
  deviceSupportsOperation,
  matchingDeviceTarget
} = require("./device_presentation");
const { desktopBuildPresentation } = require("./status_presentation");
const {
  authorOutputRoot,
  isInside,
  isSdkRoot,
  readSdkMetadata,
  resolveSdkRoot,
  SDK_INSTALL_METADATA_FILENAME
} = require("./author_environment");
const {
  downloadLatestSdk,
  fetchLatestSdkRelease,
  sdkInstallName
} = require("./sdk_download");

let outputChannel;
let reportPanel;
let capabilityDiagnostics;
let lastReport;
let lastReportCommand;
let lastPackageRoot;
let lastCapturePath;
let lastDeviceDiscovery;
let lastDeviceInfo;
let lastDeviceApps;
let lastDeviceEndpoint;
let activeDeviceOperation;
let lastDeviceFailure;
let lastDeviceLifecycle;
let statusProvider;
let embeddedDebugSession;
let activeDesktopBuildSetup;

const APP_ID_PATTERN = /^[a-zA-Z0-9][a-zA-Z0-9_.-]*$/;
const DIRECTORY_NAME_PATTERN = /^[^<>:"/\\|?*\x00-\x1f.][^<>:"/\\|?*\x00-\x1f]*$/;
const FONT_BUDGET_PATTERN = /^[1-9][0-9]*x[1-9][0-9]*$/;

function config() {
  return vscode.workspace.getConfiguration("jellyframe");
}

function repoRoot(context) {
  const configured = String(config().get("sdkRoot", "") || config().get("repoRoot", "") || "").trim();
  return resolveSdkRoot({
    workspaceRoot: workspaceFolderPath(),
    configuredRoot: configured,
    extensionPath: context.extensionPath
  }) || configured || workspaceFolderPath() || path.resolve(context.extensionPath, "..", "..");
}

function cliPath(context) {
  return path.join(repoRoot(context), "tools", "jellyframe_cli.py");
}

function requireAuthorSdk(context) {
  const sdkRoot = resolvedAuthorSdk(context);
  if (sdkRoot && isSdkRoot(sdkRoot)) {
    return sdkRoot;
  }
  const chinese = isChinese();
  const configure = chinese ? "配置作者环境" : "Configure author environment";
  const message = chinese
    ? "此操作需要 JellyFrame 作者 SDK，但当前尚未配置。请选择从 GitHub 下载官方 SDK，或选择已安装的 SDK。"
    : "This operation needs a JellyFrame App Author SDK, but none is configured. Download the official SDK from GitHub or select an installed SDK.";
  ensureOutputChannel().appendLine(`[warning] ${message}`);
  vscode.window.showWarningMessage(message, configure).then((choice) => {
    if (choice === configure) {
      manageAuthorEnvironment(context);
    }
  });
  return undefined;
}

function buildDir(context) {
  const configured = String(config().get("buildDir", "") || "").trim();
  if (configured) {
    return path.isAbsolute(configured)
      ? configured
      : path.resolve(repoRoot(context), configured);
  }
  return authorOutputRoot(workspaceFolderPath(), repoRoot(context)) || path.join(repoRoot(context), "build");
}

function ensureBuildDir(context) {
  fs.mkdirSync(buildDir(context), { recursive: true });
}

function appRequiresScripting(root) {
  try {
    const manifestPath = packageManifestPath(root);
    if (!manifestPath) {
      return false;
    }
    const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
    const mode = manifest?.runtime?.script ?? manifest?.script;
    return typeof mode === "string" && mode !== "" && mode !== "none";
  } catch (_) {
    return false;
  }
}

function nativeBuildDir(context, preferScripting = false) {
  const configured = String(config().get("buildDir", "") || "").trim();
  const resolvedConfigured = configured
    ? (path.isAbsolute(configured) ? configured : path.resolve(repoRoot(context), configured))
    : "";
  return selectBuildDirectory(repoRoot(context), resolvedConfigured, preferScripting);
}

function buildDirectoryError(context, selection) {
  const chinese = /^zh(?:-|$)/i.test(vscode.env.language || "");
  const code = selection?.issue?.code;
  if (code === "legacy-script-task-option") {
    return chinese
      ? "桌面构建仍使用已废弃的 JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME。请清除该构建目录后，使用 JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME 重新配置。"
      : "The desktop build still uses deprecated JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME. Clear that build directory and reconfigure with JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME.";
  }
  if (code === "scripting-disabled") {
    return chinese
      ? "当前 App 需要脚本构建，但所选桌面构建未启用 JELLYFRAME_BUILD_SCRIPTING=ON。"
      : "This App needs a scripting build, but the selected desktop build was configured without JELLYFRAME_BUILD_SCRIPTING=ON.";
  }
  if (code === "missing-directory" || code === "missing-cache") {
    return chinese
      ? "所选桌面构建不是可用的 CMake 构建输出。请在设置中选择实际的 Release/Debug 输出目录。"
      : "The selected desktop build is not a usable CMake build output. Choose the actual Release/Debug output directory in Settings.";
  }
  if (selection?.issue?.requiresScripting) {
    return chinese
      ? "未找到当前的脚本桌面构建。请配置 build/desktop-scripting-release（或 desktop-scripting-debug），并启用 JELLYFRAME_BUILD_SCRIPTING=ON。"
      : "No current scripting desktop build was found. Configure build/desktop-scripting-release (or desktop-scripting-debug) with JELLYFRAME_BUILD_SCRIPTING=ON.";
  }
  return chinese
    ? "未找到当前的桌面构建。请配置 build/desktop-release 或 build/desktop-debug。"
    : "No current desktop build was found. Configure build/desktop-release or build/desktop-debug.";
}

function desktopBuildQuickFixLabel(selection, requiresScripting) {
  const chinese = isChinese();
  if (selection?.issue?.code === "legacy-script-task-option") {
    return chinese ? "重建桌面构建" : "Recreate desktop build";
  }
  return requiresScripting || selection?.issue?.requiresScripting
    ? (chinese ? "创建脚本桌面构建" : "Create scripting desktop build")
    : (chinese ? "创建桌面构建" : "Create desktop build");
}

function requireNativeBuildDir(context, preferScripting = false) {
  const selection = nativeBuildDir(context, preferScripting);
  if (selection.buildDirectory) {
    return selection.buildDirectory;
  }
  const message = buildDirectoryError(context, selection);
  ensureOutputChannel().appendLine(`JellyFrame build selection: ${message}`);
  const setup = desktopBuildQuickFixLabel(selection, preferScripting);
  vscode.window.showErrorMessage(message, setup).then((choice) => {
    if (choice === setup) {
      configureDesktopBuild(context, preferScripting);
    }
  });
  return undefined;
}

function processFailureDetail(stdout, stderr) {
  const lines = `${stderr || ""}\n${stdout || ""}`.split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean);
  const detail = lines[lines.length - 1] || "";
  return detail.length > 320 ? `${detail.slice(0, 319)}...` : detail;
}

function runLocalTool(context, executable, args, options = {}) {
  const label = options.label || executable;
  const channel = ensureOutputChannel();
  channel.appendLine(`+ ${[executable, ...args].join(" ")}`);
  return new Promise((resolve) => {
    let stdout = "";
    let stderr = "";
    let completed = false;
    const finish = (outcome) => {
      if (!completed) {
        completed = true;
        resolve(outcome);
      }
    };
    const append = (stream, chunk) => {
      const captured = appendBoundedOutput(stream === "stdout" ? stdout : stderr, chunk.toString());
      if (stream === "stdout") {
        stdout = captured.text;
      } else {
        stderr = captured.text;
      }
      if (captured.appended) {
        channel.append(captured.appended);
      }
    };
    let child;
    try {
      child = childProcess.spawn(executable, args, {
        cwd: options.cwd || repoRoot(context),
        shell: false,
        windowsHide: true
      });
    } catch (error) {
      const message = isChinese()
        ? `${label}无法启动：${error.message}`
        : `${label} could not start: ${error.message}`;
      channel.appendLine(`[error] ${message}`);
      vscode.window.showErrorMessage(message);
      finish({ code: undefined, stdout, stderr, error });
      return;
    }
    child.stdout?.on("data", (chunk) => append("stdout", chunk));
    child.stderr?.on("data", (chunk) => append("stderr", chunk));
    child.on("error", (error) => {
      const message = isChinese()
        ? `${label}无法启动：${error.message}`
        : `${label} could not start: ${error.message}`;
      channel.appendLine(`[error] ${message}`);
      vscode.window.showErrorMessage(message);
      finish({ code: undefined, stdout, stderr, error });
    });
    child.on("close", (code, signal) => {
      if (completed) {
        return;
      }
      const outcome = { code, signal, stdout, stderr };
      channel.appendLine(`${label} exited with code ${code ?? "unknown"}`);
      if (code !== 0) {
        const detail = processFailureDetail(stdout, stderr);
        const message = isChinese()
          ? `${label}失败${detail ? `：${detail}` : "。请查看 JellyFrame 运行日志。"}`
          : `${label} failed${detail ? `: ${detail}` : ". Open the JellyFrame run log."}`;
        channel.appendLine(`[error] ${message}`);
        vscode.window.showErrorMessage(message);
        outcome.error = new Error(message);
      }
      finish(outcome);
    });
  });
}

function managedBuildRecreateReason(plan) {
  const cache = path.join(plan.buildRoot, "CMakeCache.txt");
  try {
    const text = fs.readFileSync(cache, "utf8");
    if (text.includes("JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME")) {
      return "legacy-script-task-option";
    }
    if (process.platform === "win32" && !text.includes("CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022")) {
      return "incompatible-generator";
    }
    return undefined;
  } catch (_) {
    return undefined;
  }
}

async function configureDesktopBuild(context, scripting) {
  if (activeDesktopBuildSetup) {
    vscode.window.showInformationMessage(isChinese()
      ? "JellyFrame 正在创建桌面构建。"
      : "JellyFrame is already creating a desktop build.");
    return activeDesktopBuildSetup;
  }
  if (process.platform !== "win32") {
    vscode.window.showErrorMessage(isChinese()
      ? "桌面壳快速构建目前仅支持 Windows。"
      : "Desktop-shell quick setup is currently supported on Windows only.");
    return undefined;
  }
  if (!requireAuthorSdk(context)) {
    return undefined;
  }

  const root = repoRoot(context);
  if (!fs.existsSync(path.join(root, "CMakeLists.txt"))) {
    const message = isChinese()
      ? "当前 JellyFrame SDK 未包含本机构建文件。请安装带桌面运行时的 SDK，或改为选择完整框架源码。"
      : "The current JellyFrame SDK does not include local build files. Install an SDK with a desktop runtime, or select a full framework source checkout.";
    ensureOutputChannel().appendLine(`[error] ${message}`);
    vscode.window.showErrorMessage(message);
    return undefined;
  }
  const jerry = jerryscriptState(root);
  const plan = desktopBuildPlan(root, {
    scripting,
    jerryscriptRoot: scripting && jerry.sourceAvailable ? jerry.sourceDirectory : ""
  });
  const setup = async (progress) => {
    progress.report({ message: isChinese() ? "检查桌面构建配置..." : "Checking desktop build configuration..." });
    if (scripting && !jerry.sourceAvailable) {
      const message = isChinese()
        ? "未找到 third_party/jerryscript。请先按脚本构建文档获取 JerryScript 源码，再重试。"
        : "third_party/jerryscript was not found. Obtain the JerryScript source as documented, then retry.";
      ensureOutputChannel().appendLine(`[error] ${message}`);
      vscode.window.showErrorMessage(message);
      return undefined;
    }
    const recreateReason = managedBuildRecreateReason(plan);
    if (recreateReason) {
      const recreate = isChinese() ? "删除并重建" : "Delete and recreate";
      const reason = recreateReason === "legacy-script-task-option"
        ? (isChinese() ? "使用已废弃的 script-task cache 项" : "uses the deprecated script-task cache entry")
        : (isChinese() ? "使用了不兼容的 CMake generator" : "uses an incompatible CMake generator");
      const choice = await vscode.window.showWarningMessage(
        isChinese()
          ? `桌面构建 ${plan.buildRoot} ${reason}。仅该生成目录会被删除并重新创建。`
          : `Desktop build ${plan.buildRoot} ${reason}. Only this generated directory will be deleted and recreated.`,
        { modal: true }, recreate);
      if (choice !== recreate) {
        return undefined;
      }
      const managed = managedProfileRoot(root, plan.outputDirectory);
      if (!managed) {
        vscode.window.showErrorMessage(isChinese()
          ? "拒绝删除非 JellyFrame 管理的构建目录。请在设置中选择其他目录。"
          : "Refusing to delete a build directory not managed by JellyFrame. Choose another directory in Settings.");
        return undefined;
      }
      fs.rmSync(managed, { recursive: true, force: true });
    }
    if (scripting && !jerry.librariesAvailable) {
      progress.report({ message: isChinese() ? "构建 JerryScript..." : "Building JerryScript..." });
      const dependency = await runLocalTool(
        context,
        config().get("pythonPath", "python"),
        jerryscriptBuildArguments(jerry.sourceDirectory),
        { label: isChinese() ? "构建 JerryScript" : "Build JerryScript", cwd: jerry.sourceDirectory }
      );
      if (dependency.code !== 0) {
        return undefined;
      }
    }
    progress.report({ message: isChinese() ? "配置桌面构建..." : "Configuring desktop build..." });
    const configured = await runLocalTool(context, "cmake", plan.configureArguments, {
      label: isChinese() ? "配置 JellyFrame 桌面构建" : "Configure JellyFrame desktop build",
      cwd: root
    });
    if (configured.code !== 0) {
      return undefined;
    }
    progress.report({ message: isChinese() ? "编译桌面壳..." : "Building desktop shell..." });
    const built = await runLocalTool(context, "cmake", plan.buildArguments, {
      label: isChinese() ? "构建 JellyFrame 桌面壳" : "Build JellyFrame desktop shell",
      cwd: root
    });
    if (built.code !== 0 || !fs.existsSync(path.join(plan.outputDirectory, "jellyframe_desktop_shell.exe"))) {
      if (built.code === 0) {
        vscode.window.showErrorMessage(isChinese()
          ? "构建完成但未生成 JellyFrame 桌面壳。请查看 JellyFrame 运行日志。"
          : "The build completed but did not produce the JellyFrame desktop shell. Open the JellyFrame run log.");
      }
      return undefined;
    }
    const configuredBuild = String(config().get("buildDir", "") || "").trim();
    if (configuredBuild) {
      const target = vscode.workspace.workspaceFolders?.length
        ? vscode.ConfigurationTarget.Workspace
        : vscode.ConfigurationTarget.Global;
      await config().update("buildDir", path.relative(root, plan.outputDirectory), target);
    }
    ensureOutputChannel().appendLine(`JellyFrame desktop build ready: ${plan.outputDirectory}`);
    vscode.window.showInformationMessage(isChinese()
      ? "JellyFrame 脚本桌面构建已就绪。现在可以重新执行调试或程控回放。"
      : "The JellyFrame scripting desktop build is ready. You can retry debugging or programmed playback.");
    statusProvider?.refresh();
    return plan.outputDirectory;
  };
  activeDesktopBuildSetup = vscode.window.withProgress({
    location: vscode.ProgressLocation.Notification,
    title: isChinese() ? "JellyFrame 正在创建桌面构建" : "JellyFrame is creating a desktop build",
    cancellable: false
  }, setup);
  statusProvider?.refresh();
  try {
    return await activeDesktopBuildSetup;
  } finally {
    activeDesktopBuildSetup = undefined;
    statusProvider?.refresh();
  }
}

async function selectAuthorSdk(context, preferredSdk) {
  const chinese = isChinese();
  const workspace = workspaceFolderPath();
  let selected = preferredSdk;
  if (!selected || !isSdkRoot(selected)) {
    const picked = await vscode.window.showOpenDialog({
      canSelectFiles: false,
      canSelectFolders: true,
      canSelectMany: false,
      openLabel: chinese ? "选择 JellyFrame SDK 文件夹" : "Select JellyFrame SDK folder",
      title: chinese ? "选择已安装的 JellyFrame SDK" : "Select an installed JellyFrame SDK"
    });
    selected = picked?.[0]?.fsPath;
  }
  if (!selected || !isSdkRoot(selected)) {
    const message = chinese
      ? "未找到有效的 JellyFrame SDK。该文件夹必须包含 tools/jellyframe_cli.py。"
      : "No valid JellyFrame SDK was found. The folder must contain tools/jellyframe_cli.py.";
    vscode.window.showErrorMessage(message);
    return undefined;
  }
  await config().update("sdkRoot", selected, vscode.ConfigurationTarget.Global);
  if (workspace && !isInside(workspace, selected)) {
    const descriptorDirectory = path.join(workspace, ".jellyframe");
    const descriptorPath = path.join(descriptorDirectory, "project.json");
    if (!fs.existsSync(descriptorPath)) {
      fs.mkdirSync(descriptorDirectory, { recursive: true });
      fs.writeFileSync(descriptorPath, JSON.stringify({
        format: "jellyframe.app.project",
        formatVersion: 1,
        sdkRoot: selected
      }, null, 2) + "\n", "utf8");
    }
  }
  const message = chinese
    ? `JellyFrame 作者环境已配置：${selected}`
    : `JellyFrame author environment configured: ${selected}`;
  ensureOutputChannel().appendLine(message);
  vscode.window.showInformationMessage(message);
  statusProvider?.refresh();
  const root = currentPackageRoot();
  const scripting = appRequiresScripting(root);
  const selection = nativeBuildDir(context, scripting);
  if (!selection.buildDirectory) {
    const setup = chinese ? "创建桌面构建" : "Create desktop build";
    const choice = await vscode.window.showInformationMessage(
      chinese ? "作者环境已连接，但尚未找到可用的桌面运行时。" : "The author environment is connected, but no desktop runtime build was found.",
      setup
    );
    if (choice === setup) {
      return configureDesktopBuild(context, scripting);
    }
  }
  return selected;
}

function wait(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

function sdkInstallFailureMessage(error, installPath, chinese) {
  const code = String(error?.code || "").toUpperCase();
  if (code === "EPERM" || code === "EACCES") {
    return chinese
      ? `Windows 未允许写入 SDK 目录：${installPath}。这通常是文件占用、杀毒软件扫描或受保护目录权限所致。扩展已自动重试；请关闭占用该目录的程序，或选择“其他位置”安装到有写权限的个人目录。`
      : `Windows did not allow writing the SDK directory: ${installPath}. This is usually caused by a file lock, antivirus scan, or a protected location. The extension already retried; close programs using the directory or choose another writable personal folder.`;
  }
  if (code === "ENOTEMPTY" || code === "EEXIST") {
    return chinese
      ? `SDK 目标目录已存在：${installPath}。扩展不会覆盖或删除其中的内容；可使用现有 SDK，或选择其他安装位置。`
      : `The SDK destination already exists: ${installPath}. The extension will not overwrite or delete its contents; use the existing SDK or choose another install location.`;
  }
  return chinese
    ? `安装 SDK 时发生文件系统错误：${error?.message || "未知错误"}`
    : `A file-system error occurred while installing the SDK: ${error?.message || "unknown error"}`;
}

async function moveSdkDirectoryWithRetry(source, destination) {
  let failure;
  for (let attempt = 0; attempt < 3; attempt += 1) {
    try {
      if (fs.existsSync(destination)) {
        const error = new Error("SDK destination already exists");
        error.code = "EEXIST";
        throw error;
      }
      fs.renameSync(source, destination);
      return;
    } catch (error) {
      failure = error;
      const retryable = ["EPERM", "EACCES", "EBUSY"].includes(String(error?.code || "").toUpperCase());
      if (!retryable || attempt === 2) {
        throw error;
      }
      await wait(250 * (attempt + 1));
    }
  }
  throw failure;
}

async function resolveExistingSdkDestination(context, installPath) {
  const chinese = isChinese();
  const existing = isSdkRoot(installPath);
  const useExisting = chinese ? "使用现有 SDK" : "Use existing SDK";
  const chooseAnother = chinese ? "选择其他位置" : "Choose another location";
  const choices = existing ? [useExisting, chooseAnother] : [chooseAnother];
  const message = existing
    ? (chinese
      ? `SDK 目标目录已存在：${installPath}。为保护已有文件，扩展不会覆盖它。`
      : `The SDK destination already exists: ${installPath}. The extension will not overwrite it.`)
    : (chinese
      ? `SDK 目标目录已存在但不是有效 SDK：${installPath}。扩展不会覆盖或删除它。`
      : `The SDK destination exists but is not a valid SDK: ${installPath}. The extension will not overwrite or remove it.`);
  const choice = await vscode.window.showWarningMessage(message, ...choices);
  if (choice === useExisting) {
    await selectAuthorSdk(context, installPath);
    return "used-existing";
  }
  if (choice === chooseAnother) {
    await downloadAuthorSdk(context);
  }
  return undefined;
}

async function downloadAuthorSdk(context, preferredParent) {
  const chinese = isChinese();
  let parent = preferredParent;
  if (!parent) {
    const picked = await vscode.window.showOpenDialog({
      canSelectFiles: false,
      canSelectFolders: true,
      canSelectMany: false,
      openLabel: chinese ? "选择 SDK 安装位置" : "Select SDK install location",
      title: chinese ? "选择 JellyFrame SDK 的父目录" : "Select a parent folder for the JellyFrame SDK"
    });
    parent = picked?.[0]?.fsPath;
  }
  if (!parent) {
    return undefined;
  }
  if (!fs.existsSync(parent) || !fs.statSync(parent).isDirectory()) {
    vscode.window.showErrorMessage(chinese ? "SDK 安装位置不是有效文件夹。" : "The SDK install location is not a valid folder.");
    return undefined;
  }

  let download;
  try {
    let previousBytes = 0;
    download = await vscode.window.withProgress({
      location: vscode.ProgressLocation.Notification,
      title: chinese ? "正在下载 JellyFrame App 作者 SDK" : "Downloading the JellyFrame App Author SDK",
      cancellable: false
    }, async (progress) => downloadLatestSdk({
      onProgress: ({ received, total }) => {
        const increment = total > 0
          ? Math.max(0, Math.min(100, (received - previousBytes) / total * 100))
          : undefined;
        previousBytes = received;
        progress.report({
          increment,
          message: total > 0
            ? `${Math.floor(received / 1024)} / ${Math.ceil(total / 1024)} KiB`
            : `${Math.floor(received / 1024)} KiB`
        });
      }
    }));
  } catch (error) {
    const message = chinese
      ? `下载 SDK 失败：${error.message}`
      : `Failed to download the SDK: ${error.message}`;
    ensureOutputChannel().appendLine(`[error] ${message}`);
    vscode.window.showErrorMessage(message);
    return undefined;
  }

  const installName = sdkInstallName(download.assetName);
  const installPath = path.join(parent, installName);
  if (fs.existsSync(installPath)) {
    fs.rmSync(download.temporaryDirectory, { recursive: true, force: true });
    return resolveExistingSdkDestination(context, installPath);
  }

  const staging = fs.mkdtempSync(path.join(parent, ".jellyframe-sdk-install-"));
  try {
    const extraction = await runLocalTool(context, config().get("pythonPath", "python"), [
      path.join(context.extensionPath, "sdk_archive.py"),
      download.archivePath,
      staging
    ], {
      label: chinese ? "解压 JellyFrame SDK" : "Extract JellyFrame SDK"
    });
    if (extraction.code !== 0) {
      return undefined;
    }
    const result = JSON.parse(extraction.stdout.trim());
    const rootName = typeof result.root === "string" ? result.root : "";
    const extractedRoot = path.join(staging, rootName);
    if (!rootName || !isSdkRoot(extractedRoot)) {
      throw new Error(chinese ? "下载的归档不是有效 JellyFrame SDK。" : "The downloaded archive is not a valid JellyFrame SDK.");
    }
    await moveSdkDirectoryWithRetry(extractedRoot, installPath);
    fs.writeFileSync(path.join(installPath, SDK_INSTALL_METADATA_FILENAME), JSON.stringify({
      format: "jellyframe.sdk-install",
      formatVersion: 1,
      releaseTag: download.releaseTag,
      assetName: download.assetName,
      sha256: download.expectedDigest,
      installedAt: new Date().toISOString()
    }, null, 2) + "\n", "utf8");
    await selectAuthorSdk(context, installPath);
    const message = chinese
      ? `JellyFrame SDK 已安装：${installPath}`
      : `JellyFrame SDK installed: ${installPath}`;
    ensureOutputChannel().appendLine(`${message} (${download.releaseTag}, sha256:${download.expectedDigest})`);
    vscode.window.showInformationMessage(message);
    return installPath;
  } catch (error) {
    const message = sdkInstallFailureMessage(error, installPath, chinese);
    ensureOutputChannel().appendLine(`[error] ${message}`);
    const retry = chinese ? "重试" : "Retry";
    const chooseAnother = chinese ? "选择其他位置" : "Choose another location";
    const useExisting = isSdkRoot(installPath) ? (chinese ? "使用现有 SDK" : "Use existing SDK") : undefined;
    const choice = await vscode.window.showErrorMessage(message, ...[retry, chooseAnother, useExisting].filter(Boolean));
    if (choice === retry) {
      return downloadAuthorSdk(context, parent);
    }
    if (choice === chooseAnother) {
      return downloadAuthorSdk(context);
    }
    if (choice === useExisting) {
      return selectAuthorSdk(context, installPath);
    }
    return undefined;
  } finally {
    fs.rmSync(staging, { recursive: true, force: true });
    fs.rmSync(download.temporaryDirectory, { recursive: true, force: true });
  }
}

function resolvedAuthorSdk(context) {
  return resolveSdkRoot({
    workspaceRoot: workspaceFolderPath(),
    configuredRoot: String(config().get("sdkRoot", "") || config().get("repoRoot", "") || "").trim(),
    extensionPath: context.extensionPath
  });
}

async function checkAuthorSdkUpdate(context, sdkDirectory) {
  const chinese = isChinese();
  const installed = readSdkMetadata(sdkDirectory);
  let latest;
  try {
    latest = await vscode.window.withProgress({
      location: vscode.ProgressLocation.Notification,
      title: chinese ? "正在检查 JellyFrame SDK 更新" : "Checking for JellyFrame SDK updates",
      cancellable: false
    }, () => fetchLatestSdkRelease());
  } catch (error) {
    const message = chinese
      ? `无法检查 SDK 更新：${error.message}`
      : `Unable to check for SDK updates: ${error.message}`;
    ensureOutputChannel().appendLine(`[error] ${message}`);
    vscode.window.showErrorMessage(message);
    return;
  }

  if (installed?.releaseTag === latest.releaseTag) {
    vscode.window.showInformationMessage(chinese
      ? `当前已使用最新 App 作者 SDK（${latest.releaseTag}）。`
      : `This workspace already uses the latest App Author SDK (${latest.releaseTag}).`);
    return;
  }

  const download = chinese ? "下载并安装最新 SDK" : "Download and install latest SDK";
  const detail = installed?.releaseTag
    ? (chinese
      ? `当前 SDK：${installed.releaseTag}；最新发布：${latest.releaseTag}。`
      : `Current SDK: ${installed.releaseTag}; latest release: ${latest.releaseTag}.`)
    : (chinese
      ? `当前 SDK 未记录下载来源；最新发布：${latest.releaseTag}。`
      : `The current SDK has no recorded download provenance; latest release: ${latest.releaseTag}.`);
  const choice = await vscode.window.showInformationMessage(detail, download);
  if (choice === download) {
    await downloadAuthorSdk(context);
  }
}

async function manageAuthorEnvironment(context) {
  const chinese = isChinese();
  const sdkDirectory = resolvedAuthorSdk(context);
  if (!sdkDirectory) {
    const picked = await vscode.window.showQuickPick([
      {
        label: chinese ? "从 GitHub 下载 App 作者 SDK" : "Download App Author SDK from GitHub",
        description: chinese ? "下载官方 Release，校验 SHA-256 后安装。" : "Download the official Release and verify SHA-256 before installation.",
        action: "download"
      },
      {
        label: chinese ? "选择已安装的 JellyFrame SDK" : "Select an installed JellyFrame SDK",
        description: chinese ? "选择包含 tools/jellyframe_cli.py 的 SDK 或源码根目录。" : "Choose an SDK or source root containing tools/jellyframe_cli.py.",
        action: "select"
      }
    ], {
      title: chinese ? "配置 JellyFrame 作者环境" : "Configure JellyFrame author environment",
      placeHolder: chinese ? "选择作者 SDK 的来源" : "Choose an App Author SDK source"
    });
    if (picked?.action === "download") {
      await downloadAuthorSdk(context);
    } else if (picked?.action === "select") {
      await selectAuthorSdk(context);
    }
    return;
  }

  const metadata = readSdkMetadata(sdkDirectory);
  const version = metadata?.releaseTag || metadata?.runtimeVersion || path.basename(sdkDirectory);
  const picked = await vscode.window.showQuickPick([
    {
      label: chinese ? "检查 SDK 更新" : "Check for SDK updates",
      description: chinese ? `当前：${version}` : `Current: ${version}`,
      action: "update"
    },
    {
      label: chinese ? "选择其他已安装 SDK" : "Select another installed SDK",
      description: chinese ? "切换当前工作区使用的 JellyFrame SDK。" : "Change the JellyFrame SDK used by this workspace.",
      action: "select"
    },
    {
      label: chinese ? "在资源管理器中打开 SDK" : "Open SDK in Explorer",
      description: sdkDirectory,
      action: "open"
    }
  ], {
    title: chinese ? `JellyFrame 作者环境：${version}` : `JellyFrame author environment: ${version}`,
    placeHolder: chinese ? "选择要执行的环境操作" : "Choose an environment action"
  });
  if (picked?.action === "update") {
    await checkAuthorSdkUpdate(context, sdkDirectory);
  } else if (picked?.action === "select") {
    await selectAuthorSdk(context);
  } else if (picked?.action === "open") {
    await vscode.commands.executeCommand("revealFileInOS", vscode.Uri.file(sdkDirectory));
  }
}

function debugLauncherPath(context) {
  return path.join(repoRoot(context), "tools", "debug", "jellyframe_debug.py");
}

function ensureOutputChannel(reveal = false) {
  if (!outputChannel) {
    outputChannel = vscode.window.createOutputChannel("JellyFrame");
  }
  if (reveal) {
    outputChannel.show(true);
  }
  return outputChannel;
}

function showOutputChannel() {
  ensureOutputChannel(true);
}

function configuredDeviceProvider(context) {
  const provider = config().get("deviceProvider", "").trim();
  const chinese = /^zh(?:-|$)/i.test(vscode.env.language || "");
  const openSettings = chinese ? "打开设置" : "Open Settings";
  if (!provider || !path.isAbsolute(provider)) {
    const message = chinese
      ? "请先配置 JellyFrame: Device Provider，并填写 provider 可执行文件的绝对路径。"
      : "Configure JellyFrame: Device Provider with an absolute provider path first.";
    vscode.window.showWarningMessage(message, openSettings).then((choice) => {
      if (choice) {
        vscode.commands.executeCommand("workbench.action.openSettings", "@ext:jellyframe.jellyframe-tools jellyframe.deviceProvider");
      }
    });
    return undefined;
  }
  let providerIsFile = false;
  try {
    providerIsFile = fs.statSync(provider).isFile();
  } catch (_) {
    providerIsFile = false;
  }
  if (!providerIsFile) {
    const message = chinese
      ? `Device Provider 不存在或不是文件：${provider}`
      : `Device Provider does not exist or is not a file: ${provider}`;
    vscode.window.showErrorMessage(message, openSettings).then((choice) => {
      if (choice) {
        vscode.commands.executeCommand("workbench.action.openSettings", "@ext:jellyframe.jellyframe-tools jellyframe.deviceProvider");
      }
    });
    return undefined;
  }
  return provider;
}

function deviceCliArguments(context, provider) {
  const args = ["device", "--provider", provider];
  const manifest = config().get("deviceManifest", "").trim();
  if (manifest) {
    args.push("--manifest", path.isAbsolute(manifest) ? manifest : path.resolve(repoRoot(context), manifest));
  }
  return args;
}

function isChinese() {
  return /^zh(?:-|$)/i.test(vscode.env.language || "");
}

function commandLabel(options) {
  return options.failureLabel || options.commandName || (isChinese() ? "JellyFrame 命令" : "JellyFrame command");
}

function showCommandFailure(failure) {
  const channel = ensureOutputChannel();
  channel.appendLine(`[error] ${failure.message}`);
  const action = isChinese() ? "查看运行日志" : "View run log";
  vscode.window.showErrorMessage(failure.message, action).then((choice) => {
    if (choice === action) {
      showOutputChannel();
    }
  });
}

function invokeCallback(callback, ...argumentsList) {
  if (!callback) {
    return undefined;
  }
  try {
    return callback(...argumentsList);
  } catch (error) {
    ensureOutputChannel().appendLine(`[error] JellyFrame extension callback failed: ${error.message}`);
    return error;
  }
}

function clearDeviceFailure() {
  lastDeviceFailure = undefined;
}

function selectedDeviceRecord() {
  return Array.isArray(lastDeviceDiscovery)
    ? lastDeviceDiscovery.find((device) => device?.endpointId === lastDeviceEndpoint)
    : undefined;
}

function parseDeviceCommandOutput(stdout, operation) {
  const parsed = JSON.parse(stdout);
  const events = Array.isArray(parsed) ? parsed : [parsed];
  const terminal = events[events.length - 1];
  if (!terminal || typeof terminal !== "object" || terminal.operation !== operation ||
      typeof terminal.resultCode !== "string") {
    throw new Error("device operation returned an invalid terminal result");
  }
  return { events, terminal };
}

function recordDeviceLifecycle(operation, terminal) {
  lastDeviceLifecycle = {
    operation,
    resultCode: terminal.resultCode,
    message: terminal.message || "",
    transaction: terminal.transaction,
    progress: terminal.progress,
    recovery: terminal.recovery,
    logSummary: terminal.logSummary
  };
  statusProvider?.refresh();
}

function recordDeviceFailure(operation, failure) {
  lastDeviceFailure = {
    operation,
    resultCode: failure.resultCode,
    message: failure.message
  };
  statusProvider?.refresh();
}

async function runDeviceCommand(context, operation, args, options = {}) {
  if (activeDeviceOperation) {
    const message = isChinese()
      ? `设备操作正在进行：${activeDeviceOperation}。请等待它完成后再试。`
      : `A device operation is already running: ${activeDeviceOperation}. Wait for it to finish before retrying.`;
    ensureOutputChannel().appendLine(`[warning] ${message}`);
    vscode.window.showWarningMessage(message);
    return { skipped: true };
  }
  activeDeviceOperation = operation;
  statusProvider?.refresh();
  try {
    const endpoint = options.endpointId ? ` · ${options.endpointId}` : "";
    return await vscode.window.withProgress({
      location: vscode.ProgressLocation.Notification,
      title: `JellyFrame: ${operation}${endpoint}`,
      cancellable: false
    }, () => runCliWithOptions(context, args, {
        ...options,
        failureLabel: options.failureLabel || operation,
        onFailure: (failure, outcome) => {
          recordDeviceFailure(operation, failure);
          invokeCallback(options.onFailure, failure, outcome);
        }
      }));
  } finally {
    activeDeviceOperation = undefined;
    statusProvider?.refresh();
  }
}

async function discoverDevice(context) {
  const provider = configuredDeviceProvider(context);
  if (!provider) {
    return;
  }
  const args = deviceCliArguments(context, provider);
  args.push("discover");
  const outcome = await runDeviceCommand(context, isChinese() ? "发现设备" : "Discover device", args, {
    onStdout: (stdout) => {
      const result = JSON.parse(stdout);
      if (result?.resultCode !== "ok" || !Array.isArray(result.devices)) {
        throw new Error("device discovery returned no device list");
      }
      lastDeviceDiscovery = result.devices;
      lastDeviceInfo = undefined;
      lastDeviceApps = undefined;
      lastDeviceEndpoint = undefined;
      lastDeviceLifecycle = undefined;
      clearDeviceFailure();
      statusProvider?.refresh();
    },
    onFailure: () => {
      lastDeviceDiscovery = undefined;
      lastDeviceInfo = undefined;
      lastDeviceApps = undefined;
      lastDeviceEndpoint = undefined;
      lastDeviceLifecycle = undefined;
      statusProvider?.refresh();
    }
  });
  if (outcome?.code !== 0 || !Array.isArray(lastDeviceDiscovery)) {
    return;
  }
  const selected = await selectDiscoveredDevice({ forceSelection: lastDeviceDiscovery.length > 1 });
  const summary = discoverySummary(lastDeviceDiscovery, isChinese());
  if (selected) {
    vscode.window.showInformationMessage(`${summary} ${isChinese() ? "当前设备：" : "Selected device: "}${deviceSummary(selected.device, isChinese())}`);
  } else {
    vscode.window.showInformationMessage(summary);
  }
}

function setSelectedDevice(selected) {
  if (lastDeviceEndpoint === selected.endpointId) {
    return;
  }
  lastDeviceEndpoint = selected.endpointId;
  lastDeviceInfo = undefined;
  lastDeviceApps = undefined;
  lastDeviceLifecycle = undefined;
  statusProvider?.refresh();
}

async function selectDiscoveredDevice(options = {}) {
  const chinese = /^zh(?:-|$)/i.test(vscode.env.language || "");
  const devices = Array.isArray(lastDeviceDiscovery)
    ? lastDeviceDiscovery.filter((device) => device && typeof device.endpointId === "string")
    : [];
  if (devices.length === 0) {
    vscode.window.showWarningMessage(chinese
      ? "请先成功发现 Device OS 设备，再使用设备功能。"
      : "Discover a configured Device OS endpoint before using device commands.");
    return;
  }
  const choices = devices.map((device) => ({ ...deviceChoice(device, chinese), device }));
  const existing = choices.find((choice) => choice.endpointId === lastDeviceEndpoint);
  const selected = existing && !options.forceSelection
    ? existing
    : (choices.length === 1 && !options.forceSelection
    ? choices[0]
    : await vscode.window.showQuickPick(choices, {
      placeHolder: chinese ? "选择本次操作的 JellyFrame Device OS 设备" : "Select the JellyFrame Device OS device for this operation",
      activeItem: existing
    }));
  if (!selected) {
    return;
  }
  setSelectedDevice(selected);
  return selected;
}

async function chooseDevice() {
  const selected = await selectDiscoveredDevice({ forceSelection: true });
  if (selected) {
    vscode.window.showInformationMessage((isChinese() ? "当前设备：" : "Selected device: ") + deviceSummary(selected.device, isChinese()));
  }
}

async function inspectDevice(context) {
  const provider = configuredDeviceProvider(context);
  if (!provider) {
    return;
  }
  const selected = await selectDiscoveredDevice();
  if (!selected) {
    return;
  }
  const args = deviceCliArguments(context, provider);
  args.push("info", "--selector", selected.endpointId);
  const outcome = await runDeviceCommand(context, isChinese() ? "读取设备身份" : "Read device identity", args, {
    endpointId: selected.endpointId,
    onStdout: (stdout) => {
      const result = JSON.parse(stdout);
      if (result?.device?.endpointId !== selected.endpointId || !result.identity) {
        throw new Error("device identity did not match the selected endpoint");
      }
      lastDeviceInfo = result;
      clearDeviceFailure();
      statusProvider?.refresh();
    },
    onFailure: () => {
      lastDeviceInfo = undefined;
      statusProvider?.refresh();
    }
  });
  if (outcome?.code === 0 && lastDeviceInfo?.identity) {
    vscode.window.showInformationMessage(identitySummary(lastDeviceInfo.device, lastDeviceInfo.identity, isChinese()));
  }
}

async function listDeviceApps(context, options = {}) {
  const provider = configuredDeviceProvider(context);
  if (!provider) {
    return;
  }
  const selected = await selectDiscoveredDevice();
  if (!selected) {
    return;
  }
  const args = deviceCliArguments(context, provider);
  args.push("list", "--selector", selected.endpointId);
  const outcome = await runDeviceCommand(context, isChinese() ? "列出已安装 App" : "List installed Apps", args, {
    endpointId: selected.endpointId,
    onStdout: (stdout) => {
      const result = JSON.parse(stdout);
      if (result?.device?.endpointId !== selected.endpointId ||
          !Array.isArray(result.apps) ||
          !Number.isInteger(result.registryGeneration)) {
        throw new Error("missing selected-device app list");
      }
      lastDeviceApps = {
        endpointId: selected.endpointId,
        apps: result.apps,
        registryGeneration: result.registryGeneration
      };
      clearDeviceFailure();
      statusProvider?.refresh();
    },
    onFailure: () => {
      lastDeviceApps = undefined;
      statusProvider?.refresh();
    }
  });
  if (outcome?.code === 0 && lastDeviceApps && !options.silent) {
    const text = isChinese()
      ? `${selected.endpointId}：${lastDeviceApps.apps.length} 个已安装 App，registry generation ${lastDeviceApps.registryGeneration}。`
      : `${selected.endpointId}: ${lastDeviceApps.apps.length} installed App(s), registry generation ${lastDeviceApps.registryGeneration}.`;
    vscode.window.showInformationMessage(text);
  }
}

async function ensureDeviceApps(context) {
  if (lastDeviceApps?.endpointId === lastDeviceEndpoint) {
    return lastDeviceApps.apps;
  }
  await listDeviceApps(context, { silent: true });
  return lastDeviceApps?.endpointId === lastDeviceEndpoint ? lastDeviceApps.apps : undefined;
}

async function chooseInstalledDeviceApp(context, operation, options = {}) {
  const apps = await ensureDeviceApps(context);
  if (!Array.isArray(apps) || apps.length === 0) {
    vscode.window.showWarningMessage(isChinese()
      ? "当前设备没有可用于此操作的已安装 App。"
      : "The selected device has no installed App for this operation.");
    return undefined;
  }
  const candidates = apps.filter((app) => !options.requireRollback || app.rollbackAvailable);
  if (candidates.length === 0) {
    vscode.window.showWarningMessage(isChinese()
      ? "当前设备没有可回滚的 App。"
      : "The selected device has no App with a rollback version.");
    return undefined;
  }
  const selected = await vscode.window.showQuickPick(candidates.map((app) => ({
    label: app.appId,
    description: `${app.versionName || "?"} · ${app.state || "?"}`,
    detail: app.rollbackAvailable
      ? (isChinese() ? "保留回滚版本" : "Rollback version available")
      : (isChinese() ? "无回滚版本" : "No rollback version"),
    app
  })), {
    placeHolder: isChinese()
      ? `选择要${operation}的 App`
      : `Select the App to ${operation}`
  });
  return selected?.app;
}

async function runDeviceLifecycleCommand(context, operation, providerArguments, options = {}) {
  const provider = configuredDeviceProvider(context);
  if (!provider) {
    return undefined;
  }
  const selected = await selectDiscoveredDevice();
  if (!selected) {
    return undefined;
  }
  if (!deviceSupportsOperation(selected.device, operation)) {
    vscode.window.showWarningMessage(isChinese()
      ? `当前 Provider 未声明支持“${operation}”，因此不会执行此设备操作。`
      : `The configured Provider did not declare support for '${operation}', so no device operation was started.`);
    return undefined;
  }
  const args = deviceCliArguments(context, provider);
  args.push(operation, "--selector", selected.endpointId, ...providerArguments);
  const outcome = await runDeviceCommand(context, options.label || operation, args, {
    endpointId: selected.endpointId,
    onStdout: (stdout) => {
      const { events, terminal } = parseDeviceCommandOutput(stdout, operation);
      if (terminal.device?.endpointId !== selected.endpointId) {
        throw new Error("device lifecycle result did not attest the selected endpoint");
      }
      recordDeviceLifecycle(operation, terminal);
      if (operation === "logs") {
        for (const event of events.filter((event) => event?.kind === "log" && event.log)) {
          const log = event.log;
          ensureOutputChannel().appendLine(`[device ${log.level}] ${log.appId} #${log.generation}: ${log.message}`);
        }
      }
      clearDeviceFailure();
    },
    onFailure: () => {
      lastDeviceLifecycle = undefined;
      statusProvider?.refresh();
    }
  });
  if (outcome?.code === 0 && options.refreshApps) {
    await listDeviceApps(context, { silent: true });
  }
  return outcome;
}

async function deployDeviceApp(context, resourceUri) {
  const selected = await selectDiscoveredDevice();
  if (!selected || !deviceSupportsOperation(selected.device, "install")) {
    return;
  }
  const root = await packageRoot(resourceUri);
  if (!root) {
    return;
  }
  const manifestPath = packageManifestPath(root);
  const manifest = readJsonObject(manifestPath);
  const appId = typeof manifest?.id === "string" && manifest.id ? manifest.id : path.basename(root);
  const deviceTarget = matchingDeviceTarget(manifest, selected.device);
  if (!deviceTarget) {
    const display = selected.device?.capabilities?.display;
    const size = Number.isInteger(display?.width) && Number.isInteger(display?.height)
      ? `${display.width} x ${display.height}` : "the selected display";
    vscode.window.showErrorMessage(isChinese()
      ? `App 没有声明与 ${size} 匹配的唯一 target，未开始部署。请在 jellyframe.app.json 的 targets 中声明设备 profile。`
      : `The App does not declare one target matching ${size}; deployment was not started. Declare the device profile in jellyframe.app.json targets.`);
    return;
  }
  const action = isChinese() ? "打包并部署" : "Package and Deploy";
  const confirmed = await vscode.window.showWarningMessage(
    isChinese()
      ? `将打包 ${appId} 并部署到 ${selected.endpointId}。已有相同 App 会作为更新处理。`
      : `Package ${appId} and deploy it to ${selected.endpointId}. An existing App with the same identity will be updated.`,
    { modal: true }, action);
  if (confirmed !== action) {
    return;
  }
  ensureBuildDir(context);
  const base = outputBase(root);
  const bundleDirectory = path.join(buildDir(context), "device-bundles");
  fs.mkdirSync(bundleDirectory, { recursive: true });
  const bundle = path.join(bundleDirectory, `${base}.jfapp`);
  const report = path.join(buildDir(context), `vscode-${base}-device-package-report.json`);
  const packageOutcome = await runCliWithOptions(context, [
    "package", "--root", root, "--target", deviceTarget, "--report", report, "--output-bundle", bundle
  ], { commandName: isChinese() ? "打包设备 App" : "Package device App", reportPath: report });
  if (packageOutcome?.code !== 0 || !fs.existsSync(bundle)) {
    return;
  }
  await runDeviceLifecycleCommand(context, "install", ["--bundle", bundle], {
    label: isChinese() ? "部署 App" : "Deploy App",
    refreshApps: true
  });
}

async function runSelectedAppLifecycle(context, operation, options = {}) {
  const selected = await selectDiscoveredDevice();
  if (!selected) {
    return;
  }
  if (!deviceSupportsOperation(selected.device, operation)) {
    vscode.window.showWarningMessage(isChinese()
      ? `当前 Provider 未声明支持“${operation}”，因此不会执行此设备操作。`
      : `The configured Provider did not declare support for '${operation}', so no device operation was started.`);
    return;
  }
  const app = await chooseInstalledDeviceApp(context, options.chineseVerb || operation, options);
  if (!app) {
    return;
  }
  if (operation === "remove") {
    const remove = isChinese() ? "删除 App" : "Remove App";
    const keepData = isChinese() ? "删除并保留数据" : "Remove and keep data";
    const choice = await vscode.window.showWarningMessage(
      isChinese() ? `从设备删除 ${app.appId}？` : `Remove ${app.appId} from the device?`,
      { modal: true }, remove, keepData);
    if (!choice) {
      return;
    }
    await runDeviceLifecycleCommand(context, operation,
      ["--id", app.appId, ...(choice === keepData ? ["--keep-data"] : [])],
      { label: remove, refreshApps: true });
    return;
  }
  await runDeviceLifecycleCommand(context, operation, ["--id", app.appId], {
    label: isChinese() ? `${options.chineseVerb || operation} App` : `${operation} App`,
    refreshApps: operation !== "logs"
  });
}

async function inspectDeviceRecovery(context) {
  await runDeviceLifecycleCommand(context, "recovery", [], {
    label: isChinese() ? "读取恢复状态" : "Read recovery status"
  });
}

function runCli(context, args) {
  return runCliWithOptions(context, args, {});
}

function runCliWithOptions(context, args, options = {}) {
  if (!requireAuthorSdk(context)) {
    return Promise.resolve({ code: undefined, stdout: "", stderr: "", missingSdk: true });
  }
  const python = config().get("pythonPath", "python");
  const cli = cliPath(context);
  const channel = ensureOutputChannel();
  const commandArgs = [cli, ...args];
  if (options.reportPath && fs.existsSync(options.reportPath)) {
    fs.rmSync(options.reportPath, { force: true });
  }
  channel.appendLine(`+ ${[python, ...commandArgs].join(" ")}`);
  return new Promise((resolve) => {
    let child;
    let completed = false;
    let stdout = "";
    let stderr = "";
    let stdoutTruncated = false;
    let stderrTruncated = false;

    const finish = (outcome) => {
      if (!completed) {
        completed = true;
        resolve(outcome);
      }
    };
    const append = (stream, chunk) => {
      const text = chunk.toString();
      const captured = appendBoundedOutput(stream === "stdout" ? stdout : stderr, text);
      if (stream === "stdout") {
        stdout = captured.text;
        if (captured.truncated && !stdoutTruncated) {
          stdoutTruncated = true;
          channel.appendLine("\n[warning] JellyFrame stdout was truncated after 1 MiB.");
        }
      } else {
        stderr = captured.text;
        if (captured.truncated && !stderrTruncated) {
          stderrTruncated = true;
          channel.appendLine("\n[warning] JellyFrame stderr was truncated after 1 MiB.");
        }
      }
      if (captured.appended) {
        channel.append(captured.appended);
      }
    };
    const fail = (failure, outcome) => {
      showCommandFailure(failure);
      invokeCallback(options.onFailure, failure, outcome);
      invokeCallback(options.onClose, outcome.code, outcome);
      finish(outcome);
    };

    try {
      child = childProcess.spawn(python, commandArgs, {
        cwd: repoRoot(context),
        shell: false
      });
    } catch (error) {
      const failure = commandFailure({ operation: commandLabel(options), chinese: isChinese(), internalError: error.message });
      fail(failure, { code: undefined, stdout, stderr, failure });
      return;
    }
    child.stdout?.on("data", (chunk) => append("stdout", chunk));
    child.stderr?.on("data", (chunk) => append("stderr", chunk));
    child.on("error", (error) => {
      const failure = commandFailure({ operation: commandLabel(options), stdout, stderr, chinese: isChinese(), internalError: error.message });
      fail(failure, { code: undefined, stdout, stderr, failure });
    });
    child.on("close", (code, signal) => {
      if (completed) {
        return;
      }
      const outcome = { code, signal, stdout, stderr, stdoutTruncated, stderrTruncated };
      channel.appendLine(`JellyFrame command exited with code ${code ?? "unknown"}`);
      if (options.reportPath && fs.existsSync(options.reportPath)) {
        loadReport(options.reportPath, options.commandName);
      }
      if (options.packageRoot && options.reportPath && fs.existsSync(options.reportPath)) {
        updateReportDiagnostics(options.packageRoot);
        showReportPanel(context);
      }
      if (code !== 0) {
        const failure = commandFailure({ operation: commandLabel(options), stdout, stderr, chinese: isChinese() });
        outcome.failure = failure;
        fail(failure, outcome);
        return;
      }
      if (options.onStdout) {
        const callbackError = invokeCallback(options.onStdout, stdout);
        if (callbackError) {
          const failure = commandFailure({
            operation: commandLabel(options),
            stdout,
            stderr,
            chinese: isChinese(),
            internalError: callbackError.message
          });
          outcome.failure = failure;
          fail(failure, outcome);
          return;
        }
      }
      if (options.capture && config().get("openCaptureAfterRun", true)) {
        openCaptureFile(options.capture);
      }
      invokeCallback(options.onClose, code, outcome);
      finish(outcome);
    });
  });
}

function loadReport(reportPath, commandName) {
  try {
    if (fs.existsSync(reportPath)) {
      lastReport = JSON.parse(fs.readFileSync(reportPath, "utf8"));
      lastReportCommand = commandName || undefined;
    }
  } catch (error) {
    ensureOutputChannel().appendLine(`failed to read report ${reportPath}: ${error.message}`);
  }
}

function runDetachedPython(context, script, args, options = {}) {
  if (!requireAuthorSdk(context)) {
    return undefined;
  }
  if (!fs.existsSync(script)) {
    vscode.window.showErrorMessage(`Missing JellyFrame debug tool: ${script}`);
    return;
  }
  const python = config().get("pythonPath", "python");
  const channel = ensureOutputChannel();
  const commandArgs = [script, ...args];
  channel.appendLine(`+ ${[python, ...commandArgs].join(" ")}`);
  let child;
  let completed = false;
  let stdout = "";
  let stderr = "";
  const finish = (outcome) => {
    if (!completed) {
      completed = true;
      invokeCallback(options.onClose, outcome.code, outcome);
    }
  };
  const append = (stream, chunk) => {
    const text = chunk.toString();
    const captured = appendBoundedOutput(stream === "stdout" ? stdout : stderr, text);
    if (stream === "stdout") {
      stdout = captured.text;
    } else {
      stderr = captured.text;
    }
    if (captured.appended) {
      channel.append(captured.appended);
    }
  };
  try {
    child = childProcess.spawn(python, commandArgs, {
      cwd: repoRoot(context),
      detached: !options.wait,
      stdio: options.wait ? "pipe" : "ignore",
      windowsHide: false
    });
  } catch (error) {
    const failure = commandFailure({ operation: options.failureLabel || "JellyFrame debug command", chinese: isChinese(), internalError: error.message });
    showCommandFailure(failure);
    finish({ code: undefined, stdout, stderr, failure });
    return undefined;
  }
  if (options.wait) {
    child.stdout?.on("data", (chunk) => append("stdout", chunk));
    child.stderr?.on("data", (chunk) => append("stderr", chunk));
  }
  child.on("error", (error) => {
    const failure = commandFailure({ operation: options.failureLabel || "JellyFrame debug command", stdout, stderr, chinese: isChinese(), internalError: error.message });
    showCommandFailure(failure);
    finish({ code: undefined, stdout, stderr, failure });
  });
  child.on("close", (code, signal) => {
    if (completed) {
      return;
    }
    const outcome = { code, signal, stdout, stderr };
    channel.appendLine(`JellyFrame debug command exited with code ${code ?? "unknown"}`);
    if (code !== 0) {
      outcome.failure = commandFailure({ operation: options.failureLabel || "JellyFrame debug command", stdout, stderr, chinese: isChinese() });
      showCommandFailure(outcome.failure);
      finish(outcome);
      return;
    }
    if (options.capture && config().get("openCaptureAfterRun", true)) {
      openCaptureFile(options.capture);
    }
    finish(outcome);
  });
  if (!options.wait) {
    child.unref();
  }
  return child;
}

function openCaptureFile(filePath) {
  if (!filePath || !fs.existsSync(filePath)) {
    vscode.window.showWarningMessage(`Capture not found: ${filePath || "none"}`);
    return;
  }
  lastCapturePath = filePath;
  vscode.commands.executeCommand("vscode.open", vscode.Uri.file(filePath));
}

function workspaceFolderPath() {
  const folders = vscode.workspace.workspaceFolders;
  if (!folders || folders.length === 0) {
    return undefined;
  }
  return folders[0].uri.fsPath;
}

function findPackageRootFrom(startPath) {
  if (!startPath || !fs.existsSync(startPath)) {
    return undefined;
  }
  let current;
  try {
    current = fs.statSync(startPath).isDirectory() ? startPath : path.dirname(startPath);
  } catch (_) {
    return undefined;
  }
  while (true) {
    if (isPackageRoot(current)) {
      return current;
    }
    const parent = path.dirname(current);
    if (parent === current) {
      return undefined;
    }
    current = parent;
  }
}

function isPackageRoot(root) {
  return Boolean(packageManifestPath(root));
}

function packageManifestPath(root) {
  if (!root) {
    return undefined;
  }
  for (const name of ["jellyframe.app.json", "app.json"]) {
    const candidate = path.join(root, name);
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }
  return undefined;
}

function currentPackageRoot() {
  const active = vscode.window.activeTextEditor?.document.uri.fsPath;
  const activeRoot = findPackageRootFrom(active);
  if (activeRoot) {
    lastPackageRoot = activeRoot;
    return activeRoot;
  }
  const workspaceRoot = findPackageRootFrom(workspaceFolderPath());
  if (workspaceRoot) {
    lastPackageRoot = workspaceRoot;
    return workspaceRoot;
  }
  return lastPackageRoot && isPackageRoot(lastPackageRoot)
    ? lastPackageRoot
    : undefined;
}

async function packageRoot(resourceUri) {
  const selectedResource = resourceUri && resourceUri.fsPath;
  if (selectedResource) {
    const found = findPackageRootFrom(selectedResource);
    if (found) {
      lastPackageRoot = found;
      return found;
    }
  }
  const active = vscode.window.activeTextEditor?.document.uri.fsPath;
  if (active) {
    const found = findPackageRootFrom(active);
    if (found) {
      lastPackageRoot = found;
      return found;
    }
  }
  const workspace = workspaceFolderPath();
  if (workspace) {
    const found = findPackageRootFrom(workspace);
    if (found) {
      lastPackageRoot = found;
      return found;
    }
  }
  const selected = await vscode.window.showOpenDialog({
    canSelectFiles: false,
    canSelectFolders: true,
    canSelectMany: false,
    openLabel: "Select JellyFrame package root"
  });
  lastPackageRoot = selected && selected[0] ? selected[0].fsPath : undefined;
  return lastPackageRoot;
}

function outputBase(root) {
  return path.basename(root).replace(/[^a-zA-Z0-9_.-]/g, "_") || "app";
}

function readJsonObject(filePath) {
  try {
    const value = JSON.parse(fs.readFileSync(filePath, "utf8"));
    return value && typeof value === "object" && !Array.isArray(value) ? value : undefined;
  } catch (_) {
    return undefined;
  }
}

function viewportSummary(viewport, chinese) {
  const width = Number(viewport?.width || viewport?.designWidth || 0);
  const height = Number(viewport?.height || viewport?.designHeight || 0);
  const shape = viewport?.shape === "round"
    ? (chinese ? "圆形" : "round")
    : (viewport?.shape === "rect" ? (chinese ? "矩形" : "rectangular") : "");
  const dimensions = width > 0 && height > 0 ? `${width} x ${height}` : (chinese ? "自定义尺寸" : "custom size");
  return shape ? `${dimensions} · ${shape}` : dimensions;
}

function availableTargets(context, root, options = {}) {
  const chinese = isChinese();
  const targets = new Map();
  const presetDirectory = path.join(repoRoot(context), "tools", "presets", "targets");
  try {
    for (const name of fs.readdirSync(presetDirectory).filter((entry) => entry.endsWith(".json")).sort()) {
      const preset = readJsonObject(path.join(presetDirectory, name));
      if (typeof preset?.id === "string" && preset.id) {
        targets.set(preset.id, {
          id: preset.id,
          viewport: preset.viewport,
          detail: preset.description || (chinese ? "仓库 target preset" : "Repository target preset"),
          source: "preset"
        });
      }
    }
  } catch (_) {
    // The CLI remains the source of truth; present manifest targets if the preset directory is unavailable.
  }
  if (!options.presetOnly && root) {
    const manifest = readJsonObject(packageManifestPath(root));
    if (manifest?.targets && typeof manifest.targets === "object" && !Array.isArray(manifest.targets)) {
      for (const [id, target] of Object.entries(manifest.targets)) {
        if (!id || !target || typeof target !== "object") {
          continue;
        }
        const existing = targets.get(id);
        targets.set(id, {
          id,
          viewport: target.viewport || existing?.viewport,
          detail: existing
            ? (chinese ? "仓库 preset；当前 App 已声明" : "Repository preset; declared by this App")
            : (chinese ? "当前 App manifest 已声明" : "Declared by this App manifest"),
          source: existing ? "both" : "manifest"
        });
      }
    }
  }
  return [...targets.values()].map((target) => ({
    label: target.id,
    description: viewportSummary(target.viewport, chinese),
    detail: target.detail,
    target: target.id
  }));
}

async function selectTarget(context, root, options = {}) {
  const chinese = isChinese();
  const choices = availableTargets(context, root, options);
  if (choices.length === 0) {
    vscode.window.showErrorMessage(chinese
      ? "未找到 JellyFrame target preset。请检查 tools/presets/targets 或 App manifest。"
      : "No JellyFrame target preset was found. Check tools/presets/targets or the App manifest.");
    return undefined;
  }
  const configured = String(config().get("defaultTarget", "round-300") || "").trim();
  return vscode.window.showQuickPick(choices, {
    placeHolder: options.purpose || (chinese ? "选择目标显示形态" : "Select a target display profile"),
    activeItem: choices.find((choice) => choice.target === configured),
    ignoreFocusOut: true
  }).then((choice) => choice?.target);
}

function selectedFontBudget() {
  const value = String(config().get("fontBudget", "16x16") || "").trim();
  if (FONT_BUDGET_PATTERN.test(value)) {
    return value;
  }
  const chinese = isChinese();
  vscode.window.showErrorMessage(chinese
    ? "JellyFrame: Font Budget 必须为 WIDTHxHEIGHT 的正整数，例如 16x16。"
    : "JellyFrame: Font Budget must use positive WIDTHxHEIGHT integers, for example 16x16.");
  return undefined;
}

async function selectFrameScript(root, purpose) {
  const chinese = /^zh(?:-|$)/i.test(vscode.env.language || "");
  const choice = await vscode.window.showQuickPick([
    {
      label: chinese ? "静态" : "Static",
      description: chinese ? "只检查当前入口页面" : "Inspect the current entry page only",
      value: "static"
    },
    {
      label: chinese ? "程控回放" : "Programmed playback",
      description: chinese ? "使用 .jfcapture 驱动交互并生成报告" : "Drive interaction with a .jfcapture script and generate a report",
      value: "scripted"
    }
  ], {
    placeHolder: purpose,
    ignoreFocusOut: true
  });
  if (!choice || choice.value === "static") {
    return undefined;
  }
  const selected = await vscode.window.showOpenDialog({
    defaultUri: vscode.Uri.file(root),
    canSelectFiles: true,
    canSelectFolders: false,
    canSelectMany: false,
    filters: { "JellyFrame frame scripts": ["jfcapture"] },
    openLabel: chinese ? "选择程控脚本" : "Select playback script"
  });
  return selected && selected[0] ? selected[0].fsPath : undefined;
}

async function runPackageCommand(context, commandName, resourceUri) {
  if (!requireAuthorSdk(context)) {
    return;
  }
  const root = await packageRoot(resourceUri);
  if (!root) {
    return;
  }
  const selectedTarget = commandName === "validate" ? undefined : await selectTarget(context, root, {
    purpose: isChinese() ? "选择本次检查使用的目标显示形态" : "Select the target display profile for this check"
  });
  if (commandName !== "validate" && !selectedTarget) {
    return;
  }
  ensureBuildDir(context);
  const base = outputBase(root);
  const report = path.join(buildDir(context), `vscode-${base}-${commandName}-report.json`);
  const args = [commandName, "--root", root, "--report", report];
  if (selectedTarget) {
    args.push("--target", selectedTarget);
  }
  const options = {
    commandName,
    packageRoot: root,
    reportPath: report
  };
  if (commandName === "check") {
    const fontBudget = selectedFontBudget();
    if (!fontBudget) {
      return;
    }
    args.push("--font-budget", fontBudget);
    const frameScript = await selectFrameScript(root, /^zh(?:-|$)/i.test(vscode.env.language || "")
      ? "选择渲染验证方式"
      : "Choose render verification mode");
    if (frameScript) {
      const nativeBuildDirectory = requireNativeBuildDir(context, appRequiresScripting(root));
      if (!nativeBuildDirectory) {
        return;
      }
      const frameOutputDir = path.join(buildDir(context), "debug", `${base}-check-frames`);
      const montage = path.join(buildDir(context), "debug", `${base}-check-montage.bmp`);
      args.push(
        "--build-dir", nativeBuildDirectory,
        "--frame-script", frameScript,
        "--frame-output-dir", frameOutputDir,
        "--frame-montage", montage
      );
      options.capture = montage;
    }
  }
  if (commandName === "package") {
    args.push(
      "--output-cpp",
      path.join(buildDir(context), `vscode-${base}-resources.cpp`),
      "--debug-dir",
      path.join(buildDir(context), `vscode-${base}.jfdir`)
    );
  }
  runCliWithOptions(context, args, options);
}

async function previewPackage(context, resourceUri) {
  if (!requireAuthorSdk(context)) {
    return;
  }
  const root = await packageRoot(resourceUri);
  if (!root) {
    return;
  }
  const selectedTarget = await selectTarget(context, root, {
    purpose: isChinese() ? "选择预览使用的目标显示形态" : "Select the target display profile for preview"
  });
  if (!selectedTarget) {
    return;
  }
  const frameScript = await selectFrameScript(root, /^zh(?:-|$)/i.test(vscode.env.language || "")
    ? "选择预览方式"
    : "Choose preview mode");
  ensureBuildDir(context);
  const base = outputBase(root);
  const output = path.join(buildDir(context), `vscode-${base}.bmp`);
  const report = path.join(buildDir(context), `vscode-${base}-preview-report.json`);
  const args = ["preview", "--root", root, "--target", selectedTarget, "--output", output, "--report", report];
  if (frameScript) {
    args.push(
      "--frame-script", frameScript,
      "--frame-output-dir", path.join(buildDir(context), "debug", `${outputBase(root)}-preview-frames`)
    );
  }
  runCliWithOptions(context, args, {
    commandName: "preview",
    packageRoot: root,
    reportPath: report,
    capture: output
  });
}

async function debugExternalApp(context, resourceUri) {
  if (process.platform !== "win32") {
    vscode.window.showErrorMessage("JellyFrame desktop shell is only available on Windows.");
    return;
  }
  if (!requireAuthorSdk(context)) {
    return;
  }
  const root = await packageRoot(resourceUri);
  if (!root) {
    return;
  }
  const selectedTarget = await selectTarget(context, root, {
    purpose: isChinese() ? "选择外部调试使用的目标显示形态" : "Select the target display profile for external debugging"
  });
  if (!selectedTarget) {
    return;
  }
  const fontBudget = selectedFontBudget();
  if (!fontBudget) {
    return;
  }
  const launcher = debugLauncherPath(context);
  if (!fs.existsSync(launcher)) {
    vscode.window.showErrorMessage(`Missing debug launcher: ${launcher}`);
    return;
  }
  const nativeBuildDirectory = requireNativeBuildDir(context, appRequiresScripting(root));
  if (!nativeBuildDirectory) {
    return;
  }
  ensureBuildDir(context);
  const base = outputBase(root);
  const runtimeLog = path.join(buildDir(context), `vscode-${base}-debug-runtime.log`);
  const report = path.join(buildDir(context), `vscode-${base}-debug-report.json`);
  runDetachedPython(context, launcher, [
    "--build-dir", nativeBuildDirectory,
    "--app", root,
    "--runtime-log", runtimeLog,
    "--wait"
  ], {
    wait: true,
    onClose: (code) => {
      if (code !== 0) {
        return;
      }
      runCliWithOptions(context, [
        "check",
        "--root", root,
        "--target", selectedTarget,
        "--build-dir", nativeBuildDirectory,
        "--report", report,
        "--runtime-log", runtimeLog,
        "--font-budget", fontBudget
      ], {
        commandName: "debug",
        packageRoot: root,
        reportPath: report
      });
    }
  });
}

function embeddedDebugHtml(webview) {
  const nonce = `${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const chinese = /^zh(?:-|$)/i.test(vscode.env.language || '');
  const recordIdle = chinese ? '录制' : 'Record';
  const recordActive = chinese ? '停止录制' : 'Stop recording';
  const viewportDefault = chinese ? 'App 默认' : 'App default';
  const viewportCustom = chinese ? '自定义' : 'Custom';
  const applyViewport = chinese ? '应用并重启' : 'Apply and restart';
  const cancelViewport = chinese ? '取消' : 'Cancel';
  const resumeDebug = chinese ? '继续调试' : 'Resume';
  const restartDebug = chinese ? '重新启动' : 'Restart';
  return `<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';">
  <style>
    html, body { width: 100%; height: 100%; overflow: hidden; }
    body { --sidebar-width: 340px; --diagnostics-height: 290px; --diagnostics-green: #8fd694; box-sizing: border-box; margin: 0; color: var(--vscode-foreground); background: var(--vscode-editor-background); font-family: var(--vscode-font-family); display: grid; grid-template-rows: 42px minmax(0, 1fr) 6px var(--diagnostics-height); }
    header { display: flex; align-items: center; gap: 12px; padding: 0 12px; border-bottom: 1px solid var(--vscode-panel-border); font-size: 12px; }
    #status { color: var(--vscode-descriptionForeground); flex: 1; }
    button { appearance: none; min-width: 28px; min-height: 26px; border: 1px solid var(--vscode-button-border, transparent); color: var(--vscode-button-foreground); background: var(--vscode-button-background); cursor: pointer; }
    button:hover { background: var(--vscode-button-hoverBackground); }
    #view-controls { display: flex; align-items: center; gap: 5px; }
    #view-controls button { min-width: 30px; padding: 0 8px; }
    #zoom-fit { min-width: 44px !important; }
    #zoom-label { min-width: 54px; color: var(--vscode-descriptionForeground); font-variant-numeric: tabular-nums; text-align: right; }
    #workspace { min-width: 0; min-height: 0; display: grid; grid-template-columns: minmax(0, 1fr) 6px var(--sidebar-width); }
    #stage { min-width: 0; min-height: 0; display: flex; flex-direction: column; overflow: hidden; }
    #stage-bar { display: flex; align-items: center; gap: 8px; min-height: 34px; box-sizing: border-box; padding: 0 10px; border-bottom: 1px solid var(--vscode-panel-border); }
    .workspace-tab { min-height: 26px; padding: 0 9px; border-color: transparent; color: var(--vscode-descriptionForeground); background: transparent; font-size: 12px; }
    .workspace-tab.active { color: var(--vscode-foreground); border-bottom: 2px solid var(--vscode-focusBorder); }
    #stage-controls { display: flex; align-items: center; gap: 5px; margin-left: auto; }
    #stage-controls button { min-width: 30px; padding: 0 8px; }
    #viewport-controls { position: absolute; z-index: 1; top: 12px; right: 12px; }
    #viewport-controls select, #viewport-controls input { min-height: 25px; box-sizing: border-box; color: var(--vscode-input-foreground); background: var(--vscode-input-background); border: 1px solid var(--vscode-input-border, var(--vscode-panel-border)); }
    #viewport-controls select { max-width: 116px; }
    #viewport-controls input { width: 48px; padding: 0 4px; text-align: right; }
    #viewport-controls .times { color: var(--vscode-descriptionForeground); }
    #viewport-custom { position: absolute; top: calc(100% + 6px); right: 0; display: flex; align-items: center; gap: 4px; padding: 7px; border: 1px solid var(--vscode-panel-border); background: var(--vscode-editor-background); box-shadow: 0 1px 3px rgba(0, 0, 0, 0.24); white-space: nowrap; }
    #viewport-custom[hidden] { display: none; }
    #viewport-apply { min-width: auto !important; font-size: 11px; }
    #record.recording { color: var(--vscode-button-foreground); background: var(--vscode-testing-iconFailed, #c74e39); }
    #stage-content { position: relative; flex: 1; min-width: 0; min-height: 0; display: grid; place-items: center; padding: 18px; overflow: auto; }
    #frame { display: block; flex: none; user-select: none; outline: none; background: #111; image-rendering: auto; }
    #empty { color: var(--vscode-descriptionForeground); }
    .resizer { position: relative; z-index: 2; background: transparent; }
    .resizer:hover, .resizer:active { background: var(--vscode-focusBorder); }
    #side-resizer { cursor: col-resize; }
    #bottom-resizer { cursor: row-resize; }
    #log-panel { min-width: 0; min-height: 0; display: flex; flex-direction: column; border-left: 1px solid var(--vscode-panel-border); }
    #log-bar, #diagnostics-title { display: flex; align-items: center; gap: 6px; min-height: 34px; box-sizing: border-box; padding: 0 10px; color: var(--vscode-descriptionForeground); font-size: 12px; border-bottom: 1px solid var(--vscode-panel-border); }
    #log-title { flex: 1; }
    #log-bar button { min-width: 42px; min-height: 24px; padding: 0 7px; font-size: 11px; }
    #log-filters { display: flex; gap: 0; padding: 5px 8px; border-bottom: 1px solid var(--vscode-panel-border); overflow-x: auto; }
    .log-filter { min-width: auto; min-height: 25px; padding: 0 7px; border-color: transparent; color: var(--vscode-descriptionForeground); background: transparent; font-size: 11px; }
    .log-filter.active { color: var(--vscode-button-foreground); background: var(--vscode-button-background); }
    #log { flex: 1; min-height: 0; padding: 8px 10px; overflow: auto; font: 13px/1.45 var(--vscode-editor-font-family, monospace); background: transparent; }
    .log-entry { display: flex; align-items: flex-start; gap: 7px; padding: 3px 0; word-break: break-word; }
    .log-kind { flex: 0 0 auto; min-width: 42px; box-sizing: border-box; padding: 1px 4px; border-radius: 2px; color: var(--vscode-descriptionForeground); background: var(--vscode-textBlockQuote-background); font: 10px/1.35 var(--vscode-font-family); text-align: center; text-transform: uppercase; }
    .log-entry.info .log-kind { color: var(--vscode-terminal-ansiCyan, #75beff); }
    .log-entry.event .log-kind { color: var(--vscode-terminal-ansiBlue, #75beff); }
    .log-entry.warning .log-kind { color: var(--vscode-terminal-ansiYellow, #cca700); }
    .log-entry.error .log-kind { color: var(--vscode-terminal-ansiRed, #f48771); }
    .log-entry.system .log-kind { color: var(--vscode-terminal-ansiGreen, #89d185); }
    .log-text { min-width: 0; }
    #diagnostics { min-height: 0; border-top: 1px solid var(--vscode-panel-border); color: var(--diagnostics-green); }
    #diagnostics-title { min-height: 38px; color: var(--diagnostics-green); font-size: 13px; border-bottom-color: var(--vscode-panel-border); }
    #diagnostics-text { height: calc(100% - 38px); box-sizing: border-box; margin: 0; padding: 11px 14px; overflow: auto; white-space: pre-wrap; word-break: break-word; font: 13px/1.55 var(--vscode-editor-font-family, monospace); }
    @media (max-width: 700px) {
      body { --sidebar-width: 260px; --diagnostics-height: 250px; }
      #workspace { grid-template-columns: minmax(0, 1fr) 4px var(--sidebar-width); }
      #stage-content { padding: 10px; }
      #viewport-controls { top: 8px; right: 8px; }
    }
  </style>
</head>
<body>
  <header><strong>JellyFrame</strong><span id="status">Starting desktop shell...</span></header>
  <section id="workspace"><main id="stage"><div id="stage-bar"><button class="workspace-tab active" aria-selected="true">App viewport</button><div id="stage-controls"><button id="record" title="Record semantic interactions">${recordIdle}</button><button id="zoom-out" title="Zoom out">-</button><button id="zoom-fit" title="Fit to available space">Fit</button><button id="zoom-in" title="Zoom in">+</button><span id="zoom-label">Fit</span></div></div><div id="stage-content"><div id="viewport-controls" title="Change the desktop shell viewport"><select id="viewport-preset"><option value="default">${viewportDefault}</option><option value="172x320">172 x 320</option><option value="240x320">240 x 320</option><option value="300x300">300 x 300</option><option value="320x240">320 x 240</option><option value="390x640">390 x 640</option><option value="custom">${viewportCustom}</option></select><div id="viewport-custom" hidden><input id="viewport-width" inputmode="numeric" pattern="[0-9]*" min="64" max="2048" aria-label="Viewport width" placeholder="W"><span class="times">x</span><input id="viewport-height" inputmode="numeric" pattern="[0-9]*" min="64" max="2048" aria-label="Viewport height" placeholder="H"><button id="viewport-apply" title="${applyViewport}">${applyViewport}</button><button id="viewport-cancel" title="${cancelViewport}">${cancelViewport}</button></div></div><span id="empty">Waiting for the first frame...</span><canvas id="frame" tabindex="0" hidden aria-label="JellyFrame app frame"></canvas></div></main><div id="side-resizer" class="resizer" role="separator" aria-label="Resize live log"></div><aside id="log-panel"><div id="log-bar"><span id="log-title">Live log</span><button id="clear-log" title="Clear live log">Clear</button><button id="resume" title="${resumeDebug}" hidden>${resumeDebug}</button><button id="restart" title="${restartDebug}" hidden>${restartDebug}</button><button id="stop" title="Stop desktop shell">Stop</button></div><div id="log-filters"><button class="log-filter active" data-filter="all">All</button><button class="log-filter" data-filter="info">Info</button><button class="log-filter" data-filter="event">Events</button><button class="log-filter" data-filter="warning">Warnings</button><button class="log-filter" data-filter="error">Errors</button></div><div id="log" role="log" aria-live="polite"></div></aside></section>
  <div id="bottom-resizer" class="resizer" role="separator" aria-label="Resize session diagnostics"></div>
  <section id="diagnostics"><div id="diagnostics-title">Session diagnostics</div><pre id="diagnostics-text">Waiting for session configuration...</pre></section>
  <script nonce="${nonce}">
    const vscode = acquireVsCodeApi();
    const recordIdle = '${recordIdle}';
    const recordActive = '${recordActive}';
    const frame = document.getElementById('frame');
    const empty = document.getElementById('empty');
    const stage = document.getElementById('stage-content');
    const status = document.getElementById('status');
    const stop = document.getElementById('stop');
    const resume = document.getElementById('resume');
    const restart = document.getElementById('restart');
    const record = document.getElementById('record');
    const clearLog = document.getElementById('clear-log');
    const log = document.getElementById('log');
    const diagnostics = document.getElementById('diagnostics-text');
    const zoomOut = document.getElementById('zoom-out');
    const zoomFit = document.getElementById('zoom-fit');
    const zoomIn = document.getElementById('zoom-in');
    const zoomLabel = document.getElementById('zoom-label');
    const viewportPreset = document.getElementById('viewport-preset');
    const viewportWidth = document.getElementById('viewport-width');
    const viewportHeight = document.getElementById('viewport-height');
    const viewportApply = document.getElementById('viewport-apply');
    const viewportCancel = document.getElementById('viewport-cancel');
    const viewportCustom = document.getElementById('viewport-custom');
    const sideResizer = document.getElementById('side-resizer');
    const bottomResizer = document.getElementById('bottom-resizer');
    const viewState = vscode.getState ? (vscode.getState() || {}) : {};
    let viewport = { width: 1, height: 1 };
    let latestSequence = 0;
    let renderedSequence = 0;
    let renderToken = 0;
    let moveQueued = false;
    let pendingMove = null;
    let logLines = [];
    let logFilter = 'all';
    let recording = false;
    let sessionActive = true;
    let appliedViewport = { width: 0, height: 0 };
    let fitMode = viewState.fitMode !== false;
    let manualZoom = Math.max(0.25, Math.min(4, Number(viewState.manualZoom) || 1));
    function persistViewState() {
      vscode.setState?.({
        sidebarWidth: parseFloat(document.body.style.getPropertyValue('--sidebar-width')) || 340,
        diagnosticsHeight: parseFloat(document.body.style.getPropertyValue('--diagnostics-height')) || 290,
        fitMode,
        manualZoom
      });
    }
    function restoreDimension(variable, value, minimum, maximum) {
      const numeric = Number(value);
      if (Number.isFinite(numeric)) document.body.style.setProperty(variable, Math.max(minimum, Math.min(maximum, numeric)) + 'px');
    }
    restoreDimension('--sidebar-width', viewState.sidebarWidth, 240, Math.max(260, window.innerWidth * 0.7));
    restoreDimension('--diagnostics-height', viewState.diagnosticsHeight, 190, Math.max(220, window.innerHeight - 160));
    function fittedScale() {
      const availableWidth = Math.max(1, stage.clientWidth - 36);
      const availableHeight = Math.max(1, stage.clientHeight - 36);
      return Math.max(0.25, Math.min(4, Math.min(availableWidth / viewport.width, availableHeight / viewport.height)));
    }
    function currentScale() {
      return fitMode ? fittedScale() : manualZoom;
    }
    function updateFrameSize() {
      if (frame.hidden) return;
      const scale = currentScale();
      frame.style.width = Math.max(1, Math.round(viewport.width * scale)) + 'px';
      frame.style.height = Math.max(1, Math.round(viewport.height * scale)) + 'px';
      zoomLabel.textContent = (fitMode ? 'Fit ' : '') + Math.round(scale * 100) + '%';
    }
    function setManualZoom(nextZoom) {
      fitMode = false;
      manualZoom = Math.max(0.25, Math.min(4, nextZoom));
      updateFrameSize();
      persistViewState();
    }
    function setSessionState(state) {
      sessionActive = state === 'running';
      const stopped = state === 'stopped';
      stop.hidden = stopped;
      resume.hidden = !stopped;
      restart.hidden = !stopped;
      stop.disabled = state === 'stopping';
      record.disabled = !sessionActive;
      viewportPreset.disabled = state === 'stopping';
      viewportApply.disabled = state === 'stopping';
      viewportCancel.disabled = state === 'stopping';
    }
    function selectedViewport() {
      if (viewportPreset.value === 'default') return { width: 0, height: 0 };
      const match = viewportPreset.value.match(/^(\\d+)x(\\d+)$/);
      const width = match ? Number(match[1]) : Number(viewportWidth.value);
      const height = match ? Number(match[2]) : Number(viewportHeight.value);
      return { width, height };
    }
    function syncViewportInputs() {
      const match = viewportPreset.value.match(/^(\\d+)x(\\d+)$/);
      if (match) {
        viewportWidth.value = match[1];
        viewportHeight.value = match[2];
      }
      const custom = viewportPreset.value === 'custom';
      viewportCustom.hidden = !custom;
      viewportWidth.disabled = !custom;
      viewportHeight.disabled = !custom;
    }
    function setViewportConfig(width, height, remember) {
      const value = width > 0 && height > 0 ? width + 'x' + height : 'default';
      viewportPreset.value = Array.from(viewportPreset.options).some((item) => item.value === value) ? value : (width > 0 ? 'custom' : 'default');
      viewportWidth.value = width > 0 ? String(width) : '';
      viewportHeight.value = height > 0 ? String(height) : '';
      if (remember) appliedViewport = { width, height };
      syncViewportInputs();
    }
    function applyViewportRequest() {
      const requested = selectedViewport();
      if (requested.width === 0 && requested.height === 0) {
        vscode.postMessage({ type: 'viewport-request', width: 0, height: 0 });
        return;
      }
      if (!Number.isInteger(requested.width) || !Number.isInteger(requested.height) || requested.width < 64 || requested.width > 2048 || requested.height < 64 || requested.height > 2048) {
        status.textContent = 'Viewport must be whole numbers from 64 to 2048.';
        return;
      }
      vscode.postMessage({ type: 'viewport-request', width: requested.width, height: requested.height });
    }
    function renderLog() {
      const keepPinned = log.scrollTop + log.clientHeight >= log.scrollHeight - 12;
      log.replaceChildren();
      const visible = logLines.filter((entry) => logFilter === 'all' || entry.category === logFilter);
      if (visible.length === 0) {
        const placeholder = document.createElement('div');
        placeholder.className = 'log-entry';
        placeholder.textContent = logLines.length === 0 ? 'Waiting for shell output...' : 'No matching entries.';
        log.appendChild(placeholder);
      } else {
        for (const entry of visible) {
          const row = document.createElement('div');
          row.className = 'log-entry ' + entry.category;
          const kind = document.createElement('span');
          kind.className = 'log-kind';
          kind.textContent = entry.label;
          const text = document.createElement('span');
          text.className = 'log-text';
          text.textContent = entry.text;
          row.append(kind, text);
          log.appendChild(row);
        }
      }
      if (keepPinned) log.scrollTop = log.scrollHeight;
    }
    function appendLog(entry) {
      logLines.push(entry);
      if (logLines.length > 200) logLines = logLines.slice(-200);
      renderLog();
    }
    function point(event) {
      const rect = frame.getBoundingClientRect();
      return {
        x: Math.max(0, Math.min(viewport.width - 1, Math.round((event.clientX - rect.left) * viewport.width / Math.max(1, rect.width)))),
        y: Math.max(0, Math.min(viewport.height - 1, Math.round((event.clientY - rect.top) * viewport.height / Math.max(1, rect.height))))
      };
    }
    function sendPointer(action, event) {
      if (frame.hidden) return;
      const p = point(event);
      vscode.postMessage({ type: 'input', line: 'pointer ' + action + ' ' + p.x + ' ' + p.y + (event.buttons ? ' 1' : ' 0') });
    }
    frame.addEventListener('pointerdown', (event) => { frame.focus(); frame.setPointerCapture?.(event.pointerId); sendPointer('down', event); event.preventDefault(); });
    frame.addEventListener('pointerup', (event) => { frame.releasePointerCapture?.(event.pointerId); sendPointer('up', event); event.preventDefault(); });
    frame.addEventListener('pointermove', (event) => {
      pendingMove = event;
      if (!moveQueued) {
        moveQueued = true;
        requestAnimationFrame(() => { moveQueued = false; if (pendingMove) sendPointer('move', pendingMove); pendingMove = null; });
      }
    });
    frame.addEventListener('wheel', (event) => {
      if (event.ctrlKey) {
        const step = event.deltaY < 0 ? 1.1 : 1 / 1.1;
        setManualZoom(currentScale() * step);
        event.preventDefault();
        return;
      }
      const p = point(event);
      const delta = Math.max(-120, Math.min(120, Math.round(-event.deltaY)));
      vscode.postMessage({ type: 'input', line: 'wheel ' + p.x + ' ' + p.y + ' ' + delta });
      event.preventDefault();
    }, { passive: false });
    frame.addEventListener('keydown', (event) => {
      const keys = { Escape: 'escape', Enter: 'enter', ' ': 'space', Tab: 'tab', ArrowUp: 'up', ArrowDown: 'down', Backspace: 'backspace' };
      if (keys[event.key]) { vscode.postMessage({ type: 'input', line: 'key ' + keys[event.key] }); event.preventDefault(); }
    });
    clearLog.addEventListener('click', () => {
      logLines = [];
      renderLog();
      vscode.postMessage({ type: 'clear-log' });
    });
    record.addEventListener('click', () => {
      recording = !recording;
      record.textContent = recording ? recordActive : recordIdle;
      record.classList.toggle('recording', recording);
      if (recording) {
        logFilter = 'event';
        document.querySelectorAll('.log-filter').forEach((item) => item.classList.toggle('active', item.dataset.filter === 'event'));
        renderLog();
      }
      vscode.postMessage({ type: recording ? 'record-start' : 'record-stop' });
    });
    stop.addEventListener('click', () => vscode.postMessage({ type: 'stop' }));
    resume.addEventListener('click', () => vscode.postMessage({ type: 'resume' }));
    restart.addEventListener('click', () => vscode.postMessage({ type: 'restart' }));
    viewportPreset.addEventListener('change', () => {
      syncViewportInputs();
      if (viewportPreset.value !== 'custom') applyViewportRequest();
    });
    viewportApply.addEventListener('click', applyViewportRequest);
    viewportCancel.addEventListener('click', () => setViewportConfig(appliedViewport.width, appliedViewport.height, false));
    zoomOut.addEventListener('click', () => setManualZoom(currentScale() / 1.2));
    zoomIn.addEventListener('click', () => setManualZoom(currentScale() * 1.2));
    zoomFit.addEventListener('click', () => { fitMode = true; updateFrameSize(); persistViewState(); });
    document.querySelectorAll('.log-filter').forEach((button) => button.addEventListener('click', () => {
      logFilter = button.dataset.filter || 'all';
      document.querySelectorAll('.log-filter').forEach((item) => item.classList.toggle('active', item === button));
      renderLog();
    }));
    function installResizer(element, variable, minimum, maximum) {
      element.addEventListener('pointerdown', (startEvent) => {
        const startValue = parseFloat(getComputedStyle(document.body).getPropertyValue(variable));
        const startPoint = variable === '--sidebar-width' ? startEvent.clientX : startEvent.clientY;
        const pointerId = startEvent.pointerId;
        element.setPointerCapture?.(pointerId);
        const move = (event) => {
          const point = variable === '--sidebar-width' ? event.clientX : event.clientY;
          const value = startValue - (point - startPoint);
          document.body.style.setProperty(variable, Math.max(minimum, Math.min(maximum(), value)) + 'px');
          updateFrameSize();
        };
        const finish = () => {
          element.removeEventListener('pointermove', move);
          element.removeEventListener('pointerup', finish);
          element.removeEventListener('pointercancel', finish);
          persistViewState();
        };
        element.addEventListener('pointermove', move);
        element.addEventListener('pointerup', finish);
        element.addEventListener('pointercancel', finish);
        startEvent.preventDefault();
      });
    }
    installResizer(sideResizer, '--sidebar-width', 240, () => Math.max(260, window.innerWidth * 0.7));
    installResizer(bottomResizer, '--diagnostics-height', 190, () => Math.max(220, window.innerHeight - 160));
    new ResizeObserver(() => updateFrameSize()).observe(stage);
    syncViewportInputs();
    setSessionState('running');
    renderLog();
    vscode.postMessage({ type: 'ready' });
    window.addEventListener('message', (event) => {
      const message = event.data;
      if (message.type === 'frame' && message.sequence > latestSequence) {
        latestSequence = message.sequence;
        viewport = { width: message.width, height: message.height };
        const token = ++renderToken;
        const image = new Image();
        image.onload = () => {
          if (token !== renderToken || message.sequence <= renderedSequence) return;
          frame.width = message.width;
          frame.height = message.height;
          const context = frame.getContext('2d', { alpha: false });
          context.clearRect(0, 0, message.width, message.height);
          context.drawImage(image, 0, 0, message.width, message.height);
          renderedSequence = message.sequence;
          frame.hidden = false;
          empty.hidden = true;
          updateFrameSize();
          status.textContent = 'Frame ' + message.sequence + ' · ' + message.width + 'x' + message.height;
        };
        image.onerror = () => {
          if (token === renderToken) status.textContent = 'Frame ' + message.sequence + ' failed to decode';
        };
        image.src = message.dataUri;
      } else if (message.type === 'status') {
        status.textContent = message.text;
      } else if (message.type === 'log') {
        appendLog({
          category: message.category || 'info',
          label: message.label || 'Info',
          text: message.text || ''
        });
      } else if (message.type === 'clear-log') {
        logLines = [];
        renderLog();
      } else if (message.type === 'diagnostics') {
        diagnostics.textContent = message.text || '';
      } else if (message.type === 'record-state') {
        recording = Boolean(message.recording);
        record.textContent = recording ? recordActive : recordIdle;
        record.classList.toggle('recording', recording);
      } else if (message.type === 'session-state') {
        setSessionState(message.state);
      } else if (message.type === 'viewport-config') {
        const width = Number(message.width) || 0;
        const height = Number(message.height) || 0;
        setViewportConfig(width, height, true);
      } else if (message.type === 'reset-frame') {
        latestSequence = 0;
        renderedSequence = 0;
        frame.hidden = true;
        empty.hidden = false;
        empty.textContent = 'Waiting for the first frame...';
      }
    });
  </script>
</body>
</html>`;
}

function stopEmbeddedDebugSession(session, reason) {
  if (!session || session.stopping || session.exited || !session.active) {
    return session?.exitPromise || Promise.resolve();
  }
  session.exitPromise = new Promise((resolve) => { session.resolveExit = resolve; });
  session.stopping = true;
  session.stopReason = reason || 'user request';
  appendEmbeddedLog(session, 'lifecycle', `stop requested: ${session.stopReason}`);
  scheduleEmbeddedDiagnostics(session);
  try {
    postEmbeddedMessage(session, { type: 'status', text: session.stopReason });
  } catch (_) {
    // The panel may already be disposing; the process still needs to be stopped.
  }
  if (session.child?.stdin?.writable) {
    session.child.stdin.write('quit\n');
    session.child.stdin.end();
  }
  session.forceStopTimer = setTimeout(() => {
    if (!session.exited) {
      session.stopReason = `${session.stopReason || 'stop'} · force-terminated after timeout`;
      appendEmbeddedLog(session, 'lifecycle', `shell did not exit in time; terminating process tree pid=${session.child?.pid ?? 'unknown'}`);
      scheduleEmbeddedDiagnostics(session);
      if (session.child?.pid) {
        childProcess.spawn('taskkill', ['/pid', String(session.child.pid), '/t', '/f'], { windowsHide: true, stdio: 'ignore' });
      }
    }
  }, 2500);
  return session.exitPromise;
}

function parseEmbeddedFrameLine(line) {
  const fields = line.split('\t');
  if (fields.length !== 5 || fields[0] !== 'JF_FRAME') {
    return undefined;
  }
  const sequence = Number(fields[1]);
  const width = Number(fields[2]);
  const height = Number(fields[3]);
  if (!Number.isSafeInteger(sequence) || !Number.isInteger(width) || !Number.isInteger(height) || sequence < 1 || width < 1 || height < 1) {
    return undefined;
  }
  return { sequence, width, height, path: fields[4] };
}

function postEmbeddedMessage(session, message) {
  try {
    session.panel.webview.postMessage(message);
  } catch (_) {
    // The panel can be disposing while the child is still draining output.
  }
}

function embeddedDiagnosticsText(session) {
  const elapsed = Math.max(0, Date.now() - session.startedAt);
  return [
    `App: ${session.appRoot}`,
    `Runtime: ${session.scriptMode === 'classic' ? 'Classic scripting' : 'No script runtime'} · Build: ${session.buildProfile}`,
    `Desktop shell: ${session.shellPath} (PID ${session.child?.pid ?? 'not running'})`,
    `Launcher: ${session.launcher}`,
    `Frame cache: ${session.frameDir}`,
    `Session: ${session.exited ? 'Stopped' : session.stopping ? 'Stopping' : 'Running'} · ${elapsed} ms · exit ${session.exitCode ?? 'pending'}`,
    `Frames: ${session.deliveredFrames} displayed / ${session.announcedFrames} announced · ${session.droppedFrames} superseded · ${session.decodeErrors} read failures`,
    `Viewport: ${session.requestedViewport?.width > 0 ? `${session.requestedViewport.width}x${session.requestedViewport.height} requested` : 'App default'} · latest frame ${session.lastDeliveredSequence} · ${session.viewport.width}x${session.viewport.height}`,
    `Input: ${session.inputSent} sent · Shell output: ${session.stdoutLines} standard, ${session.stderrLines} error lines`,
    `Semantic capture: ${session.recording ? 'Recording' : 'Idle'} · ${session.recordingActions.length} actions`,
    `Stop reason: ${session.stopReason || 'None'}`
  ].join('\n');
}

function scheduleEmbeddedDiagnostics(session) {
  if (session.diagnosticsScheduled) {
    return;
  }
  session.diagnosticsScheduled = true;
  setTimeout(() => {
    session.diagnosticsScheduled = false;
    postEmbeddedMessage(session, { type: 'diagnostics', text: embeddedDiagnosticsText(session) });
  }, 80);
}

function simplifyEmbeddedLogLine(stream, line) {
  const text = String(line).trim();
  if (!text || text.startsWith('+ ')) {
    return undefined;
  }
  const mediaMatch = text.match(/css::css-media-query-not-matched.*?\((@media .+)\)$/);
  if (mediaMatch) {
    return { category: 'info', label: 'CSS', text: `Media query not matched: ${mediaMatch[1]}` };
  }
  const diagnosticsMatch = text.match(/^diagnostics:\s*(\d+)/i);
  if (diagnosticsMatch) {
    return { category: 'info', label: 'Runtime', text: `Runtime diagnostics: ${diagnosticsMatch[1]}` };
  }
  const clickMatch = text.match(/^click target=(.+)$/i);
  if (clickMatch) {
    return { category: 'event', label: 'Event', text: `Click: ${clickMatch[1]}` };
  }
  const controlStateMatch = text.match(/^control state id=([^ ]+) kind=([^ ]+) value=(.*?) checked=(0|1) selected=(-?\d+)$/i);
  if (controlStateMatch) {
    const value = decodeCaptureValue(controlStateMatch[3]) || '(empty)';
    return {
      category: 'event',
      label: 'Control',
      text: controlStateMatch[2] + '#' + controlStateMatch[1] + ' = ' + value
    };
  }
  if (stream === 'lifecycle') {
    return { category: 'system', label: 'System', text };
  }
  if (stream === 'error' || /\[(?:error|fatal)\]/i.test(text)) {
    return { category: 'error', label: 'Error', text: text.replace(/^\[(?:error|fatal)\]\s*/i, '') };
  }
  if (/\[warning\]/i.test(text)) {
    return { category: 'warning', label: 'Warning', text: text.replace(/^\[warning\]\s*/i, '') };
  }
  return { category: 'info', label: 'Info', text: text.replace(/^\[info\]\s*/i, '') };
}

function captureFrameFor(session) {
  const sequence = session.latestAnnouncedSequence || session.announcedFrames || 1;
  const start = session.recordingStartSequence || sequence;
  return Math.max(1, sequence - start + 1);
}

function addRecordingAction(session, action) {
  const last = session.recordingActions[session.recordingActions.length - 1];
  const same = last && last.frame === action.frame && last.kind === action.kind &&
    last.id === action.id && last.value === action.value;
  if (!same) {
    session.recordingActions.push(action);
  }
}

function replacePendingControlAction(session, pending, action) {
  for (let index = session.recordingActions.length - 1; index >= 0; index -= 1) {
    const current = session.recordingActions[index];
    if (current.kind === 'click-id' && current.id === pending.id && current.frame === pending.frame) {
      break;
    }
    if (current.kind === action.kind && current.id === action.id) {
      session.recordingActions[index] = action;
      return;
    }
  }
  addRecordingAction(session, action);
}

function recordSemanticLog(session, text) {
  if (!session.recording) {
    return;
  }
  const click = String(text).match(/^click target=[^#\s]+#([A-Za-z0-9_-]+)(?:\.|$)/i);
  if (click) {
    const frame = captureFrameFor(session);
    session.recordingPendingClick = { id: click[1], frame };
    addRecordingAction(session, { frame, kind: 'click-id', id: click[1], value: undefined });
    return;
  }
  if (/^click target=/i.test(String(text))) {
    session.recordingSkipped += 1;
    return;
  }
  const state = String(text).match(/^control state id=([A-Za-z0-9_-]+) kind=([^ ]+) value=(.*?) checked=(0|1) selected=(-?\d+)$/i);
  if (!state || !session.recordingPendingClick || state[1] !== session.recordingPendingClick.id) {
    return;
  }
  const pending = session.recordingPendingClick;
  const kind = state[2].toLowerCase();
  if (kind === 'checkbox' || kind === 'radio') {
    replacePendingControlAction(session, pending, { frame: pending.frame, kind: 'set-checked', id: pending.id, value: state[4] });
  } else if (kind === 'select' && Number(state[5]) >= 0) {
    replacePendingControlAction(session, pending, { frame: pending.frame, kind: 'select-index', id: pending.id, value: state[5] });
  } else if (kind !== 'button') {
    replacePendingControlAction(session, pending, { frame: pending.frame, kind: 'set-value', id: pending.id, value: decodeCaptureValue(state[3]) });
  }
}

function encodeCaptureValue(value) {
  return encodeURIComponent(String(value ?? ''));
}

function decodeCaptureValue(value) {
  try {
    return decodeURIComponent(String(value ?? ''));
  } catch (_) {
    return String(value ?? '');
  }
}

async function saveEmbeddedRecording(context, session) {
  if (!session.recordingActions.length) {
    vscode.window.showInformationMessage('No semantic control actions were recorded.');
    return;
  }
  const chinese = /^zh(?:-|$)/i.test(vscode.env.language || '');
  const base = outputBase(session.appRoot);
  const selected = await vscode.window.showSaveDialog({
    defaultUri: vscode.Uri.file(path.join(session.appRoot, base + '.jfcapture')),
    filters: { 'JellyFrame captures': ['jfcapture'] },
    saveLabel: chinese ? '保存录制' : 'Save capture'
  });
  if (!selected) {
    return;
  }
  const maxFrame = Math.max(...session.recordingActions.map((action) => action.frame));
  const frames = Math.max(12, maxFrame + 8);
  const viewport = session.viewport.width > 1 && session.viewport.height > 1
    ? session.viewport
    : { width: 300, height: 300 };
  const outputBaseName = path.basename(selected.fsPath, path.extname(selected.fsPath)).replace(/[^a-zA-Z0-9_.-]/g, '_') || base;
  const lines = [
    '# JellyFrame semantic interaction capture',
    'output-dir out/' + outputBaseName + '_frames',
    'montage out/' + outputBaseName + '_montage.bmp',
    'frames ' + frames,
    'step-ms 33',
    'start-ms 1000',
    'viewport ' + viewport.width + ' ' + viewport.height,
    'columns 4',
    'gap 6',
    '',
    ...session.recordingActions.map((action) => {
      const suffix = action.kind === 'set-value' ? ' ' + encodeCaptureValue(action.value) :
        action.value === undefined ? '' : ' ' + action.value;
      return 'event ' + action.frame + ' ' + action.kind + ' ' + action.id + suffix;
    })
  ];
  try {
    await fs.promises.writeFile(selected.fsPath, lines.join('\n') + '\n', 'utf8');
    vscode.window.showInformationMessage((chinese ? '已保存语义录制: ' : 'Semantic capture saved: ') + selected.fsPath);
    if (session.recordingSkipped > 0) {
      vscode.window.showWarningMessage(chinese
        ? '有 ' + session.recordingSkipped + ' 次点击缺少稳定控件 id，未写入 capture。'
        : String(session.recordingSkipped) + ' clicks had no stable control id and were not recorded.');
    }
  } catch (error) {
    vscode.window.showErrorMessage((chinese ? '保存录制失败: ' : 'Failed to save capture: ') + error.message);
  }
}

function appendEmbeddedLog(session, stream, text) {
  const lines = String(text).split(/\r?\n/).filter((line) => line.length > 0);
  if (lines.length === 0) {
    return;
  }
  session.stdoutLines += stream === 'stdout' ? lines.length : 0;
  session.stderrLines += stream === 'stderr' ? lines.length : 0;
  for (const line of lines) {
    recordSemanticLog(session, line);
    const entry = simplifyEmbeddedLogLine(stream, line);
    if (entry) {
      session.logLines.push(entry);
      if (session.webviewReady) {
        postEmbeddedMessage(session, { type: 'log', ...entry });
      }
    }
    ensureOutputChannel().appendLine(`[embedded][${stream}] ${line}`);
  }
  if (session.logLines.length > 200) {
    session.logLines = session.logLines.slice(-200);
  }
  scheduleEmbeddedDiagnostics(session);
}

async function deliverEmbeddedFrame(session, frame, runId = session.runId) {
  if (session.runId !== runId || !session.active || frame.sequence < session.latestAnnouncedSequence) {
    session.droppedFrames += 1;
    scheduleEmbeddedDiagnostics(session);
    return;
  }
  try {
    const bytes = await fs.promises.readFile(frame.path);
    if (session.runId !== runId || !session.active || frame.sequence !== session.latestAnnouncedSequence || frame.sequence <= session.lastDeliveredSequence) {
      return;
    }
    session.lastDeliveredSequence = frame.sequence;
    session.deliveredFrames += 1;
    session.viewport = { width: frame.width, height: frame.height };
    postEmbeddedMessage(session, {
      type: 'frame',
      sequence: frame.sequence,
      width: frame.width,
      height: frame.height,
      dataUri: `data:image/bmp;base64,${bytes.toString('base64')}`
    });
    scheduleEmbeddedDiagnostics(session);
  } catch (error) {
    if (error?.code === 'ENOENT') {
      // The native shell retains only a bounded latest-frame cache. A newer
      // frame can supersede this snapshot before VS Code starts the read.
      session.droppedFrames += 1;
    } else {
      session.decodeErrors += 1;
      appendEmbeddedLog(session, 'error', `failed to read frame ${frame.sequence}: ${error.message}`);
    }
    scheduleEmbeddedDiagnostics(session);
  } finally {
    fs.rm(frame.path, { force: true }, () => {});
  }
}

function validEmbeddedViewport(width, height) {
  return Number.isInteger(width) && Number.isInteger(height) &&
    width >= 64 && width <= 2048 && height >= 64 && height <= 2048;
}

function resetEmbeddedRunState(session) {
  session.active = true;
  session.stopping = false;
  session.exited = false;
  session.stopReason = undefined;
  session.exitCode = undefined;
  session.startedAt = Date.now();
  session.viewport = { width: 1, height: 1 };
  session.announcedFrames = 0;
  session.deliveredFrames = 0;
  session.droppedFrames = 0;
  session.decodeErrors = 0;
  session.inputSent = 0;
  session.stdoutLines = 0;
  session.stderrLines = 0;
  session.latestAnnouncedSequence = 0;
  session.lastDeliveredSequence = 0;
  session.outputBuffer = '';
  session.forceStopTimer = undefined;
  session.exitPromise = undefined;
  session.resolveExit = undefined;
}

function startEmbeddedDebugProcess(context, session, restartKind = 'resume') {
  if (session.disposed || session.active || session.stopping) {
    return;
  }
  ensureBuildDir(context);
  const sessionRoot = path.join(buildDir(context), 'debug', 'vscode-sessions');
  fs.mkdirSync(sessionRoot, { recursive: true });
  const frameDir = fs.mkdtempSync(path.join(sessionRoot, `${outputBase(session.appRoot)}-`));
  resetEmbeddedRunState(session);
  session.frameDir = frameDir;
  session.runId += 1;
  const runId = session.runId;
  const args = [session.launcher, '--build-dir', session.buildDir, '--app', session.appRoot,
    '--vscode-debug', '--vscode-frame-dir', frameDir, '--wait'];
  if (session.requestedViewport.width > 0) {
    args.push('--viewport-width', String(session.requestedViewport.width),
      '--viewport-height', String(session.requestedViewport.height));
  }
  const channel = ensureOutputChannel();
  channel.appendLine(`+ ${[session.python, ...args].join(' ')}`);
  let child;
  try {
    child = childProcess.spawn(session.python, args, {
      cwd: repoRoot(context),
      shell: false,
      windowsHide: true,
      stdio: ['pipe', 'pipe', 'pipe']
    });
  } catch (error) {
    session.active = false;
    session.exited = true;
    appendEmbeddedLog(session, 'error', `failed to start: ${error.message}`);
    postEmbeddedMessage(session, { type: 'status', text: `Failed to start: ${error.message}` });
    postEmbeddedMessage(session, { type: 'session-state', state: 'stopped' });
    return;
  }
  session.child = child;
  embeddedDebugSession = session;
  postEmbeddedMessage(session, { type: 'reset-frame' });
  postEmbeddedMessage(session, { type: 'session-state', state: 'running' });
  postEmbeddedMessage(session, { type: 'viewport-config', ...session.requestedViewport });
  appendEmbeddedLog(session, 'lifecycle', `${restartKind} spawn pid=${child.pid} profile=${session.buildProfile} script=${session.scriptMode}`);
  scheduleEmbeddedDiagnostics(session);
  child.stdout.on('data', (chunk) => {
    if (session.runId !== runId) return;
    session.outputBuffer += chunk.toString();
    let newline = 0;
    while ((newline = session.outputBuffer.indexOf('\n')) >= 0) {
      const line = session.outputBuffer.slice(0, newline).replace(/\r$/, '');
      session.outputBuffer = session.outputBuffer.slice(newline + 1);
      const frame = parseEmbeddedFrameLine(line);
      if (frame) {
        session.announcedFrames += 1;
        if (session.latestAnnouncedSequence > 0 && frame.sequence > session.latestAnnouncedSequence + 1) {
          session.droppedFrames += frame.sequence - session.latestAnnouncedSequence - 1;
        }
        session.latestAnnouncedSequence = Math.max(session.latestAnnouncedSequence, frame.sequence);
        void deliverEmbeddedFrame(session, frame, runId);
      } else if (line) {
        appendEmbeddedLog(session, 'stdout', line);
      }
    }
  });
  child.stderr.on('data', (chunk) => {
    if (session.runId === runId) appendEmbeddedLog(session, 'stderr', chunk.toString());
  });
  child.on('error', (error) => {
    if (session.runId !== runId) return;
    session.active = false;
    session.exited = true;
    session.stopping = false;
    session.resolveExit?.();
    appendEmbeddedLog(session, 'error', `failed to start: ${error.message}`);
    postEmbeddedMessage(session, { type: 'status', text: `Failed to start: ${error.message}` });
    postEmbeddedMessage(session, { type: 'session-state', state: 'stopped' });
    scheduleEmbeddedDiagnostics(session);
  });
  child.on('close', (code) => {
    if (session.runId !== runId) return;
    session.exited = true;
    session.active = false;
    session.stopping = false;
    session.exitCode = code;
    if (session.forceStopTimer) clearTimeout(session.forceStopTimer);
    session.resolveExit?.();
    appendEmbeddedLog(session, 'lifecycle', `shell exited with code ${code ?? 'unknown'}`);
    postEmbeddedMessage(session, { type: 'status', text: `Desktop shell stopped (exit ${code ?? 'unknown'}).` });
    postEmbeddedMessage(session, { type: 'session-state', state: 'stopped' });
    scheduleEmbeddedDiagnostics(session);
    setTimeout(() => fs.rm(frameDir, { recursive: true, force: true }, () => {}), 250);
  });
}

async function debugApp(context, resourceUri) {
  if (process.platform !== 'win32') {
    vscode.window.showErrorMessage('JellyFrame desktop shell is only available on Windows.');
    return;
  }
  if (!requireAuthorSdk(context)) return;
  const root = await packageRoot(resourceUri);
  if (!root) return;
  const launcher = debugLauncherPath(context);
  if (!fs.existsSync(launcher)) {
    vscode.window.showErrorMessage(`Missing debug launcher: ${launcher}`);
    return;
  }
  const nativeBuildDirectory = requireNativeBuildDir(context, appRequiresScripting(root));
  if (!nativeBuildDirectory) return;
  if (embeddedDebugSession && !embeddedDebugSession.disposed) {
    const previous = embeddedDebugSession;
    if (!previous.active && !previous.stopping && previous.appRoot === root) {
      previous.panel.reveal(vscode.ViewColumn.Beside);
      const resume = isChinese() ? '继续上次会话' : 'Resume previous session';
      const restart = isChinese() ? '重新启动上次会话' : 'Restart previous session';
      const choice = await vscode.window.showQuickPick([resume, restart], {
        placeHolder: isChinese() ? '已有已停止的 JellyFrame 调试会话' : 'A stopped JellyFrame debug session is available'
      });
      if (choice === resume || choice === restart) {
        if (choice === restart) {
          previous.logLines = [];
          postEmbeddedMessage(previous, { type: 'clear-log' });
        }
        startEmbeddedDebugProcess(context, previous, choice === restart ? 'restart' : 'resume');
      }
      return;
    }
    await stopEmbeddedDebugSession(previous, 'Replacing the previous debug session...');
    previous.disposed = true;
    previous.panel.dispose();
  }
  const panel = vscode.window.createWebviewPanel(
    'jellyframeEmbeddedDebug', `JellyFrame: ${path.basename(root)}`, vscode.ViewColumn.Beside,
    { enableScripts: true, retainContextWhenHidden: true }
  );
  panel.webview.html = embeddedDebugHtml(panel.webview);
  const session = {
    active: false, stopping: false, exited: true, disposed: false, runId: 0, child: undefined, panel,
    appRoot: root, scriptMode: appRequiresScripting(root) ? 'classic' : 'none',
    buildProfile: path.basename(path.dirname(nativeBuildDirectory)), python: config().get('pythonPath', 'python'),
    launcher, buildDir: nativeBuildDirectory,
    shellPath: path.join(nativeBuildDirectory, process.platform === 'win32' ? 'jellyframe_desktop_shell.exe' : 'jellyframe_desktop_shell'),
    frameDir: '', startedAt: Date.now(), viewport: { width: 1, height: 1 }, requestedViewport: { width: 0, height: 0 },
    announcedFrames: 0, deliveredFrames: 0, droppedFrames: 0, decodeErrors: 0, inputSent: 0, stdoutLines: 0, stderrLines: 0,
    logLines: [], webviewReady: false, diagnosticsScheduled: false, stopReason: undefined, exitCode: undefined,
    latestAnnouncedSequence: 0, lastDeliveredSequence: 0, recording: false, recordingStartSequence: 0,
    recordingActions: [], recordingPendingClick: undefined, recordingSkipped: 0, outputBuffer: '', forceStopTimer: undefined
  };
  embeddedDebugSession = session;
  panel.webview.onDidReceiveMessage((message) => {
    if (message?.type === 'ready') {
      session.webviewReady = true;
      postEmbeddedMessage(session, { type: 'diagnostics', text: embeddedDiagnosticsText(session) });
      postEmbeddedMessage(session, { type: 'session-state', state: session.active ? 'running' : 'stopped' });
      postEmbeddedMessage(session, { type: 'viewport-config', ...session.requestedViewport });
      for (const entry of session.logLines) postEmbeddedMessage(session, { type: 'log', ...entry });
    } else if (message?.type === 'stop') {
      postEmbeddedMessage(session, { type: 'session-state', state: 'stopping' });
      void stopEmbeddedDebugSession(session, 'Stopping desktop shell...');
    } else if (message?.type === 'resume' && !session.active && !session.stopping) {
      startEmbeddedDebugProcess(context, session, 'resume');
    } else if (message?.type === 'restart' && !session.active && !session.stopping) {
      session.logLines = [];
      postEmbeddedMessage(session, { type: 'clear-log' });
      startEmbeddedDebugProcess(context, session, 'restart');
    } else if (message?.type === 'viewport-request') {
      const width = Number(message.width);
      const height = Number(message.height);
      if (!((width === 0 && height === 0) || validEmbeddedViewport(width, height))) {
        postEmbeddedMessage(session, { type: 'status', text: 'Viewport must be whole numbers from 64 to 2048.' });
        return;
      }
      session.requestedViewport = { width, height };
      postEmbeddedMessage(session, { type: 'viewport-config', width, height });
      if (session.active || session.stopping) {
        void stopEmbeddedDebugSession(session, 'Restarting desktop shell with the requested viewport...').then(() => startEmbeddedDebugProcess(context, session, 'viewport change'));
      } else {
        startEmbeddedDebugProcess(context, session, 'viewport change');
      }
    } else if (message?.type === 'clear-log') {
      session.logLines = [];
      postEmbeddedMessage(session, { type: 'clear-log' });
    } else if (message?.type === 'record-start') {
      session.recording = true;
      session.recordingStartSequence = session.latestAnnouncedSequence || session.announcedFrames || 0;
      session.recordingActions = [];
      session.recordingPendingClick = undefined;
      session.recordingSkipped = 0;
      postEmbeddedMessage(session, { type: 'record-state', recording: true });
      appendEmbeddedLog(session, 'lifecycle', 'semantic recording started');
    } else if (message?.type === 'record-stop') {
      session.recording = false;
      session.recordingPendingClick = undefined;
      postEmbeddedMessage(session, { type: 'record-state', recording: false });
      appendEmbeddedLog(session, 'lifecycle', 'semantic recording stopped');
      void saveEmbeddedRecording(context, session);
    } else if (message?.type === 'input' && typeof message.line === 'string' && /^[a-z]+(?: [a-z-]+)?(?: -?\d+){0,4}$/.test(message.line)) {
      if (session.active && !session.stopping && session.child?.stdin?.writable) {
        session.child.stdin.write(`${message.line}\n`);
        session.inputSent += 1;
        scheduleEmbeddedDiagnostics(session);
      }
    }
  }, undefined, context.subscriptions);
  panel.onDidDispose(() => {
    session.disposed = true;
    if (embeddedDebugSession === session) embeddedDebugSession = undefined;
    void stopEmbeddedDebugSession(session, 'Debug tab closed.');
  }, undefined, context.subscriptions);
  startEmbeddedDebugProcess(context, session, 'initial');
}

async function runFrameScript(context, resourceUri) {
  if (process.platform !== "win32") {
    vscode.window.showErrorMessage("JellyFrame frame-script playback currently requires the desktop shell on Windows.");
    return;
  }
  if (!requireAuthorSdk(context)) {
    return;
  }
  const root = await packageRoot(resourceUri);
  if (!root) {
    return;
  }
  const selected = await vscode.window.showOpenDialog({
    defaultUri: vscode.Uri.file(root),
    canSelectFiles: true,
    canSelectFolders: false,
    canSelectMany: false,
    filters: { "JellyFrame frame scripts": ["jfcapture"] },
    openLabel: "Run Frame Script"
  });
  if (!selected || !selected[0]) {
    return;
  }
  const selectedTarget = await selectTarget(context, root, {
    purpose: isChinese() ? "选择程控回放使用的目标显示形态" : "Select the target display profile for frame-script playback"
  });
  if (!selectedTarget) {
    return;
  }
  const fontBudget = selectedFontBudget();
  if (!fontBudget) {
    return;
  }
  ensureBuildDir(context);
  const nativeBuildDirectory = requireNativeBuildDir(context, appRequiresScripting(root));
  if (!nativeBuildDirectory) {
    return;
  }
  const output = path.join(buildDir(context), "debug", `${outputBase(root)}-frames`);
  fs.mkdirSync(output, { recursive: true });
  const capture = path.join(output, "montage.bmp");
  const report = path.join(buildDir(context), "debug", `${outputBase(root)}-frame-script-report.json`);
  runCliWithOptions(context, [
    "preview",
    "--root", root,
    "--target", selectedTarget,
    "--build-dir", nativeBuildDirectory,
    "--output", capture,
    "--report", report,
    "--frame-script", selected[0].fsPath,
    "--frame-output-dir", output,
    "--font-budget", fontBudget
  ], {
    commandName: "frame-script",
    packageRoot: root,
    reportPath: report,
    capture
  });
}

async function openCapture(context) {
  if (lastCapturePath && fs.existsSync(lastCapturePath)) {
    openCaptureFile(lastCapturePath);
    return;
  }
  const selected = await vscode.window.showOpenDialog({
    canSelectFiles: true,
    canSelectFolders: false,
    canSelectMany: false,
    filters: { "JellyFrame captures": ["bmp", "ppm", "png"] },
    openLabel: "Open Capture"
  });
  if (selected && selected[0]) {
    openCaptureFile(selected[0].fsPath);
  }
}

function listBuilds(context) {
  if (!requireAuthorSdk(context)) {
    return;
  }
  const launcher = debugLauncherPath(context);
  runDetachedPython(context, launcher, ["--list-builds"], { wait: true });
}

function diagnosticSeverity(severity) {
  if (severity === "error") {
    return vscode.DiagnosticSeverity.Error;
  }
  if (severity === "warning") {
    return vscode.DiagnosticSeverity.Warning;
  }
  if (severity === "info") {
    return vscode.DiagnosticSeverity.Information;
  }
  return vscode.DiagnosticSeverity.Warning;
}

function diagnosticRange() {
  return new vscode.Range(new vscode.Position(0, 0), new vscode.Position(0, 1));
}

function updateReportDiagnostics(root) {
  if (!capabilityDiagnostics || !root || !lastReport) {
    return;
  }
  const diagnostics = new Map();
  const addDiagnostic = (filePath, message, severity) => {
    const items = diagnostics.get(filePath) || [];
    items.push(new vscode.Diagnostic(diagnosticRange(), message, severity));
    diagnostics.set(filePath, items);
  };
  const entryPath = path.resolve(root, String(lastReport.app?.entry || "jellyframe.app.json").replace(/^[/\\]/, ""));

  for (const advice of lastReport.developerAdvice || []) {
    const from = advice.source || lastReport.app?.entry || "/jellyframe.app.json";
    const filePath = path.resolve(root, String(from).replace(/^[/\\]/, ""));
    const target = advice.target ? ` [${advice.target}]` : "";
    const text = advice.text ? ` text="${advice.text}"` : "";
    const message = `${advice.title || advice.code || "JellyFrame advice"}${target}${text}: ${advice.action || advice.explanation || ""}`;
    addDiagnostic(filePath, message, diagnosticSeverity(advice.severity));
  }

  for (const warning of lastReport.warnings || []) {
    const from = warning.from || lastReport.app?.entry || "/jellyframe.app.json";
    const filePath = path.resolve(root, String(from).replace(/^[/\\]/, ""));
    const message = warning.message || warning.reason || JSON.stringify(warning);
    addDiagnostic(filePath, message, vscode.DiagnosticSeverity.Warning);
  }

  const pipeline = lastReport.pipelineDiagnostics || {};
  for (const diagnostic of pipeline.diagnostics || []) {
    const stage = diagnostic.stage || "pipeline";
    const code = diagnostic.code || "diagnostic";
    const detail = diagnostic.detail ? ` (${diagnostic.detail})` : "";
    const message = `${stage}::${code}: ${diagnostic.message || "Pipeline diagnostic"}${detail}`;
    addDiagnostic(entryPath, message, diagnosticSeverity(diagnostic.severity));
  }

  capabilityDiagnostics.clear();
  for (const [filePath, items] of diagnostics.entries()) {
    capabilityDiagnostics.set(vscode.Uri.file(filePath), items);
  }
  statusProvider?.refresh();
}

class JellyFrameStatusProvider {
  constructor(context) {
    this.context = context;
    this.changed = new vscode.EventEmitter();
    this.onDidChangeTreeData = this.changed.event;
  }

  refresh() {
    this.changed.fire();
  }

  getTreeItem(element) {
    return element;
  }

  getParent(element) {
    return element?.parent;
  }

  getChildren(element) {
    if (element?.children) {
      return element.children;
    }

    const root = currentPackageRoot();
    const hasPackage = Boolean(root);
    const app = hasPackage ? path.basename(root) : "No package selected";
    const selection = nativeBuildDir(this.context, appRequiresScripting(root));
    const buildDirectory = selection.buildDirectory;
    const sdkDirectory = resolveSdkRoot({
      workspaceRoot: workspaceFolderPath(),
      configuredRoot: String(config().get("sdkRoot", "") || config().get("repoRoot", "") || "").trim(),
      extensionPath: this.context.extensionPath
    });
    const sdkMetadata = readSdkMetadata(sdkDirectory);
    const desktopBuildRunning = Boolean(activeDesktopBuildSetup);
    const build = buildDirectory || buildDirectoryError(this.context, selection);
    const buildPresentation = desktopBuildPresentation(buildDirectory, /^zh(?:-|$)/i.test(vscode.env.language || ""));
    const pipeline = lastReport?.pipelineDiagnostics?.summary;
    const performance = lastReport?.performanceSummary;
    const hasRenderData = Boolean(lastReport?.pipelineDiagnostics?.format)
      || (Array.isArray(lastReport?.responsiveProfiles) && lastReport.responsiveProfiles.length > 0)
      || Boolean(lastReport?.runtimeMetrics)
      || Boolean(lastReport?.portTelemetry);
    const chinese = /^zh(?:-|$)/i.test(vscode.env.language || "");
    const selectedDevice = selectedDeviceRecord();
    const supportedDeviceOperations = advertisedDeviceOperations(selectedDevice);
    const labels = chinese ? {
      currentApp: "当前 App",
      workflow: "工作流",
      packageChecks: "检查与预览",
      interactiveDebugging: "交互式调试",
      authoring: "创建与自动化",
      reports: "报告与日志",
      environment: "环境",
      authorEnvironment: "作者环境",
      desktopRuntime: "桌面运行时",
      buildProfile: "构建配置",
      buildOutput: "输出目录",
      scriptSupport: "脚本支持",
      createDesktopBuild: "创建兼容桌面构建",
      desktopBuildInProgress: "正在创建桌面构建",
      device: "设备",
      deviceActions: "设备操作",
      deviceLifecycle: "App 生命周期与调试",
      deviceStatus: "设备状态",
      discoverDevice: "发现设备",
      selectDevice: "选择当前设备",
      inspectDevice: "读取设备身份",
      listDeviceApps: "列出已安装 App",
      deployDeviceApp: "打包并部署当前 App",
      launchDeviceApp: "启动已安装 App",
      stopDeviceApp: "停止已安装 App",
      removeDeviceApp: "删除已安装 App",
      rollbackDeviceApp: "回滚已安装 App",
      readDeviceLogs: "读取 App 日志",
      readDeviceRecovery: "读取恢复状态",
      lifecycleAvailability: "生命周期能力",
      lifecycleReadOnly: "当前 Provider 未声明生命周期操作",
      lifecycleAvailable: (count) => `已声明 ${count} 项操作`,
      lifecycleResult: "最近生命周期操作",
      noLifecycleResult: "尚未执行",
      noDeviceSession: "尚未发现设备",
      connectedDevices: "已连接",
      selectedDevice: "当前设备",
      noSelectedDevice: "尚未选择",
      deviceIdentity: "设备身份",
      noDeviceIdentity: "尚未读取",
      installedApps: "已安装 App",
      noInstalledApps: "尚未读取",
      deviceOperation: "设备操作",
      deviceReady: "可用",
      deviceFailure: "失败",
      deviceBusy: "进行中",
      package: "App 包",
      noPackage: "未识别 App",
      build: "桌面构建",
      validate: "验证 App 包结构",
      check: "检查 App 渲染",
      preview: "预览 App",
      debug: "在 VS Code 中调试",
      debugExternal: "在外部窗口调试",
      playback: "运行程控回放",
      create: "从模板新建 App",
      packageResources: "生成资源包",
      openReport: "打开最近报告",
      openCapture: "打开截图或回放文件",
      showOutput: "查看运行日志",
      reportReady: "报告已生成",
      noReport: "尚未生成报告",
      diagnostics: pipeline
        ? `${pipeline.error || 0} 个错误，${pipeline.warning || 0} 个警告`
        : "运行渲染检查后显示",
      performance: "性能摘要",
      measured: "已测量",
      notMeasured: "尚未测量",
      buildValue: buildPresentation.summary,
      sdkValue: sdkDirectory ? (sdkMetadata?.releaseTag || sdkMetadata?.runtimeVersion || path.basename(sdkDirectory)) : "未配置",
      actionHints: {
        validate: "快速检查 manifest、入口和本地资源；不启动渲染管线。",
        check: "运行渲染预检、响应式与字体检查；可选程控回放。",
        preview: "生成当前页面的静态渲染截图和报告。",
        debug: "在 VS Code 标签页中运行可交互的桌面壳。",
        debugExternal: "在独立原生窗口中运行可交互的桌面壳。",
        playback: "按 .jfcapture 脚本回放交互并生成帧证据。",
        create: "从官方模板创建一个新的 App 包。",
        packageResources: "生成供固件或 App Runtime 使用的资源包。",
        discoverDevice: "通过已配置的 Provider 列出可连接设备。",
        selectDevice: "在已发现设备中切换本次操作的目标。",
        inspectDevice: "读取并校验当前设备的 Developer Image 与 Render Core 身份。",
        listDeviceApps: "读取当前设备已安装 App 与 registry generation。",
        deployDeviceApp: "打包当前 App 为 .jfapp，并在确认后部署到选中设备。",
        launchDeviceApp: "从选中设备的安装列表选择并启动 App。",
        stopDeviceApp: "从选中设备的安装列表选择并停止 App。",
        removeDeviceApp: "从选中设备删除 App；会要求明确确认。",
        rollbackDeviceApp: "恢复某个 App 保留的上一个版本。",
        readDeviceLogs: "读取选中 App 的有界设备日志快照到 JellyFrame 运行日志。",
        readDeviceRecovery: "读取 protected launcher 与最近恢复状态。"
      }
    } : {
      currentApp: "Current App",
      workflow: "Workflow",
      packageChecks: "Check & Preview",
      interactiveDebugging: "Interactive Debugging",
      authoring: "Create & Automate",
      reports: "Reports & Logs",
      environment: "Environment",
      authorEnvironment: "Author environment",
      desktopRuntime: "Desktop Runtime",
      buildProfile: "Build profile",
      buildOutput: "Output directory",
      scriptSupport: "Script support",
      createDesktopBuild: "Create compatible desktop build",
      desktopBuildInProgress: "Creating desktop build",
      device: "Device",
      deviceActions: "Device actions",
      deviceLifecycle: "App Lifecycle & Debug",
      deviceStatus: "Device status",
      discoverDevice: "Discover device",
      selectDevice: "Select device",
      inspectDevice: "Device info",
      listDeviceApps: "List installed Apps",
      deployDeviceApp: "Package and deploy current App",
      launchDeviceApp: "Launch installed App",
      stopDeviceApp: "Stop installed App",
      removeDeviceApp: "Remove installed App",
      rollbackDeviceApp: "Roll back installed App",
      readDeviceLogs: "Read App logs",
      readDeviceRecovery: "Read recovery status",
      lifecycleAvailability: "Lifecycle capability",
      lifecycleReadOnly: "The current Provider declares no lifecycle operations",
      lifecycleAvailable: (count) => `${count} operation(s) declared`,
      lifecycleResult: "Latest lifecycle operation",
      noLifecycleResult: "Not run",
      noDeviceSession: "No device discovered",
      connectedDevices: "Connected",
      selectedDevice: "Selected device",
      noSelectedDevice: "Not selected",
      deviceIdentity: "Device identity",
      noDeviceIdentity: "Not read",
      installedApps: "Installed Apps",
      noInstalledApps: "Not read",
      deviceOperation: "Device operation",
      deviceReady: "Ready",
      deviceFailure: "Failed",
      deviceBusy: "In progress",
      package: "App package",
      noPackage: "No App detected",
      build: "Desktop build",
      validate: "Validate App package",
      check: "Check App rendering",
      preview: "Preview App",
      debug: "Debug in VS Code",
      debugExternal: "Debug in external window",
      playback: "Run programmed playback",
      create: "Create App from template",
      packageResources: "Generate resource package",
      openReport: "Open latest report",
      openCapture: "Open capture or playback file",
      showOutput: "View run log",
      reportReady: "Report ready",
      noReport: "No report yet",
      diagnostics: pipeline
        ? `${pipeline.error || 0} errors, ${pipeline.warning || 0} warnings`
        : "Run a render check to populate",
      performance: "Performance summary",
      measured: "Measured",
      notMeasured: "Not measured",
      buildValue: buildPresentation.summary,
      sdkValue: sdkDirectory ? (sdkMetadata?.releaseTag || sdkMetadata?.runtimeVersion || path.basename(sdkDirectory)) : "Not configured",
      actionHints: {
        validate: "Quickly check the manifest, entry point and local resources without starting Render Core.",
        check: "Run render preflight, responsive and font checks; optionally replay a capture.",
        preview: "Render a static capture and report for the current page.",
        debug: "Run an interactive desktop shell in a VS Code editor tab.",
        debugExternal: "Run an interactive desktop shell in a separate native window.",
        playback: "Replay a .jfcapture interaction script and produce frame evidence.",
        create: "Create a new App package from an official template.",
        packageResources: "Generate a resource package for firmware or App Runtime use.",
        discoverDevice: "List connectable devices through the configured Provider.",
        selectDevice: "Change the target for subsequent device operations.",
        inspectDevice: "Read and validate the selected Developer Image and Render Core identity.",
        listDeviceApps: "Read installed Apps and registry generation from the selected device.",
        deployDeviceApp: "Package the current App as a .jfapp and deploy it to the selected device after confirmation.",
        launchDeviceApp: "Choose and launch an App from the selected device installation list.",
        stopDeviceApp: "Choose and stop an App from the selected device installation list.",
        removeDeviceApp: "Remove an App from the selected device after explicit confirmation.",
        rollbackDeviceApp: "Restore an App's retained previous version.",
        readDeviceLogs: "Read a bounded App log snapshot into the JellyFrame run log.",
        readDeviceRecovery: "Read protected-launcher and latest recovery status."
      }
    };
    return [
      this.group(labels.currentApp, "package", [
        this.statusItem(hasPackage ? app : labels.noPackage,
          hasPackage ? labels.package : labels.noPackage,
          hasPackage ? root : labels.noPackage, "folder-opened"),
      ]),
      this.group(labels.workflow, "rocket", [
        ...(hasPackage ? [
          this.commandItem(labels.validate, labels.actionHints.validate, "jellyframe.validate", "check", root),
          this.commandItem(labels.check, labels.actionHints.check, "jellyframe.check", "check-all", root),
          this.commandItem(labels.preview, labels.actionHints.preview, "jellyframe.preview", "preview", root),
          this.commandItem(labels.debug, labels.actionHints.debug, "jellyframe.debug", "debug-alt", root),
          this.commandItem(labels.debugExternal, labels.actionHints.debugExternal, "jellyframe.debugExternal", "external-link", root),
          this.commandItem(labels.playback, labels.actionHints.playback, "jellyframe.runFrameScript", "play-circle", root),
        ] : []),
        this.commandItem(labels.create, labels.actionHints.create, "jellyframe.newFromTemplate", "new-file"),
        ...(hasPackage ? [this.commandItem(labels.packageResources, labels.actionHints.packageResources, "jellyframe.package", "package", root)] : []),
      ]),
      this.group(labels.reports, "report", [
        ...(lastReport ? [this.commandItem(labels.openReport, labels.reportReady, "jellyframe.showReport", "output")] : []),
        ...(lastCapturePath ? [this.commandItem(labels.openCapture, path.basename(lastCapturePath), "jellyframe.openCapture", "open-preview")] : []),
        this.commandItem(labels.showOutput, chinese ? "打开 JellyFrame 命令与运行日志。" : "Open JellyFrame command and runtime logs.", "jellyframe.showOutput", "output"),
        this.statusItem(chinese ? "管线诊断" : "Pipeline diagnostics", labels.diagnostics, labels.diagnostics, "pulse"),
        this.statusItem(labels.performance, hasRenderData && performance?.rating ? `${labels.measured}: ${performance.rating}` : labels.notMeasured,
          hasRenderData && performance?.rating ? `${labels.performance}: ${performance.rating}` : labels.notMeasured, "dashboard"),
      ]),
      this.group(labels.environment, "settings-gear", [
        this.statusItem(labels.authorEnvironment, labels.sdkValue,
          sdkDirectory
            ? (chinese
              ? `${sdkMetadata?.kind === "app-sdk" ? "App 作者 SDK" : "源码工作区"}：${sdkDirectory}`
              : `${sdkMetadata?.kind === "app-sdk" ? "App Author SDK" : "Source checkout"}: ${sdkDirectory}`)
            : (chinese
              ? "点击后可从 GitHub 下载官方 App 作者 SDK，或选择本机已安装的 SDK。"
              : "Click to download the official App Author SDK from GitHub or select an installed SDK."),
          sdkDirectory ? "package" : "cloud-download", "jellyframe.manageAuthorEnvironment"),
        this.statusItem(labels.build, labels.buildValue, buildPresentation.summary, "server-environment"),
        this.statusItem(labels.buildProfile, buildPresentation.profile, buildPresentation.profile, "settings-gear"),
        this.statusItem(labels.buildOutput, buildPresentation.output, buildPresentation.output, "folder"),
        this.statusItem(labels.scriptSupport, buildPresentation.scripting, buildPresentation.scripting, "symbol-event"),
        ...(desktopBuildRunning ? [this.statusItem(
          labels.desktopBuildInProgress,
          chinese ? "CMake 正在运行" : "CMake is running",
          chinese ? "正在配置或编译 JellyFrame 桌面壳。可在通知或运行日志中查看当前阶段。" : "JellyFrame is configuring or building the desktop shell. The notification and run log show the current phase.",
          "sync~spin")] : []),
        ...(!buildDirectory && !desktopBuildRunning ? [this.commandItem(
          labels.createDesktopBuild,
          chinese
            ? "创建当前 App 所需的桌面壳构建；仅在确认后运行本机 CMake。"
            : "Create the desktop-shell build needed by the current App; CMake runs only after confirmation.",
          "jellyframe.setupDesktopBuild", "tools")] : []),
        this.commandItem(chinese ? "选择或查看构建" : "Choose or inspect builds",
          chinese ? "显示可用桌面构建，并帮助确认当前选择。" : "Show available desktop builds and confirm the current selection.",
          "jellyframe.listBuilds", "list-tree"),
      ]),
      this.group(labels.device, "plug", [
        this.commandItem(labels.discoverDevice, labels.actionHints.discoverDevice, "jellyframe.deviceDiscover", "plug"),
        ...(Array.isArray(lastDeviceDiscovery) && lastDeviceDiscovery.length > 1
          ? [this.commandItem(labels.selectDevice, labels.actionHints.selectDevice, "jellyframe.deviceSelect", "symbol-array")]
          : []),
        ...(lastDeviceEndpoint
          ? [
            this.commandItem(labels.inspectDevice, labels.actionHints.inspectDevice, "jellyframe.deviceInfo", "info"),
            this.commandItem(labels.listDeviceApps, labels.actionHints.listDeviceApps, "jellyframe.deviceList", "list-tree")
          ]
          : []),
        ...(selectedDevice && supportedDeviceOperations.size > 0 ? [
          ...(hasPackage && supportedDeviceOperations.has("install")
            ? [this.commandItem(labels.deployDeviceApp, labels.actionHints.deployDeviceApp, "jellyframe.deviceDeploy", "cloud-upload", root)]
            : []),
          ...(supportedDeviceOperations.has("launch")
            ? [this.commandItem(labels.launchDeviceApp, labels.actionHints.launchDeviceApp, "jellyframe.deviceLaunch", "play")]
            : []),
          ...(supportedDeviceOperations.has("stop")
            ? [this.commandItem(labels.stopDeviceApp, labels.actionHints.stopDeviceApp, "jellyframe.deviceStop", "debug-stop")]
            : []),
          ...(supportedDeviceOperations.has("rollback")
            ? [this.commandItem(labels.rollbackDeviceApp, labels.actionHints.rollbackDeviceApp, "jellyframe.deviceRollback", "discard")]
            : []),
          ...(supportedDeviceOperations.has("remove")
            ? [this.commandItem(labels.removeDeviceApp, labels.actionHints.removeDeviceApp, "jellyframe.deviceRemove", "trash")]
            : []),
          ...(supportedDeviceOperations.has("logs")
            ? [this.commandItem(labels.readDeviceLogs, labels.actionHints.readDeviceLogs, "jellyframe.deviceLogs", "output")]
            : []),
          ...(supportedDeviceOperations.has("recovery")
            ? [this.commandItem(labels.readDeviceRecovery, labels.actionHints.readDeviceRecovery, "jellyframe.deviceRecovery", "heart")]
            : []),
        ] : []),
        this.statusItem(labels.connectedDevices,
          Array.isArray(lastDeviceDiscovery)
            ? `${lastDeviceDiscovery.filter((device) => device.connected).length}/${lastDeviceDiscovery.length}`
            : labels.noDeviceSession,
          Array.isArray(lastDeviceDiscovery) ? discoverySummary(lastDeviceDiscovery, chinese) : labels.noDeviceSession,
          "device-mobile"),
          this.statusItem(labels.selectedDevice,
          lastDeviceEndpoint || labels.noSelectedDevice,
          lastDeviceEndpoint ? (chinese ? "后续设备操作会使用此端点。" : "Subsequent device operations use this endpoint.") : labels.noSelectedDevice,
          "target"),
          this.statusItem(labels.deviceIdentity,
          lastDeviceInfo?.identity
            ? `${lastDeviceInfo.identity.renderCoreVersion || "?"} / ABI ${lastDeviceInfo.identity.renderCoreAbi ?? "?"}`
            : labels.noDeviceIdentity,
          lastDeviceInfo?.identity ? identitySummary(lastDeviceInfo.device, lastDeviceInfo.identity, chinese) : labels.noDeviceIdentity,
          "verified"),
          this.statusItem(labels.installedApps,
          lastDeviceApps
            ? `${lastDeviceApps.apps.length} · gen ${lastDeviceApps.registryGeneration}`
            : labels.noInstalledApps,
          lastDeviceApps ? (chinese ? "已读取当前设备的安装注册表。" : "The selected device installation registry was read.") : labels.noInstalledApps,
          "library"),
          this.statusItem(labels.deviceOperation,
          activeDeviceOperation
            ? `${labels.deviceBusy}: ${activeDeviceOperation}`
            : (lastDeviceFailure
              ? `${labels.deviceFailure}: ${lastDeviceFailure.operation} · ${lastDeviceFailure.resultCode}`
              : labels.deviceReady),
          lastDeviceFailure?.message || labels.deviceReady,
          activeDeviceOperation ? "sync~spin" : (lastDeviceFailure ? "warning" : "pass")),
          ...(selectedDevice ? [this.statusItem(
          labels.lifecycleAvailability,
          supportedDeviceOperations.size > 0
            ? labels.lifecycleAvailable(supportedDeviceOperations.size)
            : labels.lifecycleReadOnly,
          supportedDeviceOperations.size > 0
            ? (isChinese()
              ? "Provider 已声明的操作显示在“App 生命周期与调试”中。"
              : "Provider-declared actions are shown in App Lifecycle & Debug.")
            : (isChinese()
              ? "更新 Provider 并在发现结果中声明 supportedOperations 后，才会显示部署、启动、日志和恢复等操作。"
              : "Deploy, launch, logs and recovery appear only after an updated Provider declares supportedOperations."),
          supportedDeviceOperations.size > 0 ? "pass" : "info")]
            : []),
          this.statusItem(labels.lifecycleResult,
          lastDeviceLifecycle
            ? `${lastDeviceLifecycle.operation} · ${lastDeviceLifecycle.resultCode}`
            : labels.noLifecycleResult,
          lastDeviceLifecycle?.message || labels.noLifecycleResult,
          lastDeviceLifecycle?.resultCode === "ok" || lastDeviceLifecycle?.resultCode === "accepted" ? "pass" : "history"),
        ...(lastDeviceApps?.apps || []).map((app) => this.statusItem(
          app.appId || "unknown app",
          `${app.versionName || "?"} · ${app.state || "?"}${app.rollbackAvailable ? " · rollback" : ""}`,
          app.appId || "unknown app", "package"
        )),
      ]),
    ];
  }

  group(label, icon, children) {
    const item = new vscode.TreeItem(label, vscode.TreeItemCollapsibleState.Expanded);
    item.iconPath = icon ? new vscode.ThemeIcon(icon) : undefined;
    item.id = `group:${label}`;
    item.children = children;
    for (const child of children) {
      child.parent = item;
    }
    item.contextValue = "jellyframe.group";
    return item;
  }

  commandItem(label, description, command, icon, resource) {
    const item = new vscode.TreeItem(label, vscode.TreeItemCollapsibleState.None);
    item.description = description || undefined;
    item.tooltip = description || label;
    item.iconPath = icon ? new vscode.ThemeIcon(icon) : undefined;
    item.id = `command:${command}:${resource || ""}:${label}`;
    item.command = {
      command,
      title: label,
      arguments: resource ? [vscode.Uri.file(resource)] : []
    };
    return item;
  }

  statusItem(label, description, tooltip, icon, command) {
    const item = new vscode.TreeItem(label, vscode.TreeItemCollapsibleState.None);
    item.description = description || undefined;
    item.tooltip = tooltip || description || label;
    item.iconPath = icon ? new vscode.ThemeIcon(icon) : undefined;
    item.id = `status:${label}`;
    if (command) {
      item.command = { command, title: label };
    }
    return item;
  }
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function renderList(items, renderItem, emptyText = "None") {
  if (!items || items.length === 0) {
    return `<p class="muted">${escapeHtml(emptyText)}</p>`;
  }
  return `<ul>${items.map(renderItem).join("")}</ul>`;
}

function reportLabels() {
  const chinese = /^zh(?:-|$)/i.test(vscode.env.language || "");
  return chinese ? {
    panelTitle: "JellyFrame 报告",
    reportTitle: "JellyFrame 报告",
    packageValidationTitle: "JellyFrame 包结构验证",
    target: "目标",
    resources: "资源",
    bytes: "字节",
    packageValid: "包结构有效",
    packageValidationNote: "本报告覆盖包元数据、本地资源、引用和声明的限制；未运行 Render Core，也未测量运行时性能。",
    programmaticValidation: "程控验证",
    script: "脚本",
    status: "状态",
    runtimeLog: "运行日志",
    frameOutputDir: "帧目录",
    montage: "蒙太奇",
    authorAdvice: "作者建议",
    renderingPreflight: "渲染预检",
    rating: "评级",
    score: "评分",
    maxToolTime: "工具耗时上限",
    slowestStage: "最慢阶段",
    maxHeap: "最大堆内存",
    maxFramebuffer: "最大帧缓冲",
    maxDisplayCommands: "最大显示命令数",
    staticEstimate: "静态资源估算",
    staticEstimateNote: "本次操作只检查了包结构和资源预算，未执行 Render Core 布局、帧时序或运行时性能测试。",
    resourceBudget: "资源预算",
    measuredFrameTime: "实测帧时间",
    notAvailable: "不可用",
    pipelineDiagnostics: "管线诊断",
    total: "总数",
    errors: "错误",
    warnings: "警告",
    info: "信息",
    dom: "DOM",
    nodes: "节点",
    layout: "布局",
    boxes: "盒子",
    layers: "层",
    displayCommands: "显示命令",
    estimatedHeap: "估算堆内存",
    warningsSection: "警告",
    resourcesSection: "资源",
    references: "引用",
    path: "路径",
    kind: "类型",
    none: "无",
    missing: "缺失",
    noReport: "请先运行“验证 App 包”“检查 App 渲染”或其他会生成报告的操作。"
  } : {
    panelTitle: "JellyFrame Report",
    reportTitle: "JellyFrame Report",
    packageValidationTitle: "JellyFrame Package Validation",
    target: "Target",
    resources: "Resources",
    bytes: "Bytes",
    packageValid: "Package structure: valid",
    packageValidationNote: "This report covers package metadata, local resources, references and declared limits. It does not run Render Core or measure runtime performance.",
    programmaticValidation: "Programmed Validation",
    script: "Script",
    status: "Status",
    runtimeLog: "Runtime log",
    frameOutputDir: "Frame directory",
    montage: "Montage",
    authorAdvice: "App Author Advice",
    renderingPreflight: "Rendering Preflight",
    rating: "Rating",
    score: "Score",
    maxToolTime: "Max tool time",
    slowestStage: "Slowest stage",
    maxHeap: "Max heap",
    maxFramebuffer: "Max framebuffer",
    maxDisplayCommands: "Max display commands",
    staticEstimate: "Static Resource Estimate",
    staticEstimateNote: "This operation only checked package structure and resource budgets. Render Core layout, frame timing, and runtime performance were not executed.",
    resourceBudget: "Resource budget",
    measuredFrameTime: "Measured frame time",
    notAvailable: "not available",
    pipelineDiagnostics: "Pipeline Diagnostics",
    total: "Total",
    errors: "Errors",
    warnings: "Warnings",
    info: "Info",
    dom: "DOM",
    nodes: "nodes",
    layout: "Layout",
    boxes: "boxes",
    layers: "Layers",
    displayCommands: "Display commands",
    estimatedHeap: "Estimated heap",
    warningsSection: "Warnings",
    resourcesSection: "Resources",
    references: "References",
    path: "Path",
    kind: "Kind",
    none: "None",
    missing: "missing",
    noReport: "Run Validate App Package, Check App Rendering, or another operation that generates a report first."
  };
}

function reportHtml() {
  const report = lastReport;
  const labels = reportLabels();
  const isPackageValidation = lastReportCommand === "validate"
    || report?.reportScope === "package-validation";
  const app = report?.app || {};
  const targetConfig = report?.target || {};
  const warnings = report?.warnings || [];
  const resources = report?.resources || [];
  const references = report?.references || [];
  const developerAdvice = report?.developerAdvice || [];
  const performanceSummary = report?.performanceSummary || {};
  const performanceBottlenecks = performanceSummary?.bottlenecks || [];
  const performanceAdvice = report?.performanceAdvice || [];
  const programmaticValidation = report?.programmaticValidation;
  const pipeline = report?.pipelineDiagnostics || {};
  const summary = pipeline.summary || {};
  const pipelineStats = pipeline.pipeline || {};
  const pipelineDiagnostics = pipeline.diagnostics || [];
  const hasPipelineDiagnostics = Boolean(pipeline.format);
  const hasRenderData = hasPipelineDiagnostics
    || (Array.isArray(report?.responsiveProfiles) && report.responsiveProfiles.length > 0)
    || Boolean(report?.runtimeMetrics)
    || Boolean(report?.portTelemetry);
  return `<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <style>
    body { font-family: var(--vscode-font-family); color: var(--vscode-foreground); padding: 16px; }
    h1, h2 { margin: 0 0 12px; }
    h2 { margin-top: 22px; }
    table { border-collapse: collapse; width: 100%; }
    th, td { border-bottom: 1px solid var(--vscode-panel-border); padding: 6px; text-align: left; }
    code { color: var(--vscode-textPreformat-foreground); }
    .muted { color: var(--vscode-descriptionForeground); }
    .pill { display: inline-block; padding: 1px 6px; border: 1px solid var(--vscode-panel-border); border-radius: 10px; }
    .advice { margin-bottom: 10px; }
    .advice strong { display: block; margin-bottom: 2px; }
    .error { color: var(--vscode-errorForeground); }
    .warning { color: var(--vscode-editorWarning-foreground); }
    .info { color: var(--vscode-editorInfo-foreground); }
  </style>
</head>
<body>
  <h1>${labels.reportTitle}</h1>
  ${report ? `
    <p><strong>${escapeHtml(isPackageValidation ? labels.packageValidationTitle : labels.reportTitle)}</strong></p>
    <p><strong>${escapeHtml(app.name || app.id || "App")}</strong> <span class="muted">${escapeHtml(app.id || "")}</span></p>
    <p>${escapeHtml(labels.target)}: <code>${escapeHtml(targetConfig.id || "default")}</code> · ${escapeHtml(labels.resources)}: ${resources.length} · ${escapeHtml(labels.bytes)}: ${escapeHtml(report.totalResourceBytes || 0)}</p>
    ${isPackageValidation ? `<p class="info"><strong>${escapeHtml(labels.packageValid)}</strong> · ${escapeHtml(labels.packageValidationNote)}</p>` : ""}
    ${programmaticValidation ? `<h2>${escapeHtml(labels.programmaticValidation)}</h2>
      <p>${escapeHtml(labels.status)}: <strong>${escapeHtml(programmaticValidation.status || "unknown")}</strong> · ${escapeHtml(labels.script)}: <code>${escapeHtml(programmaticValidation.frameScript || "")}</code></p>
      <p class="muted">${escapeHtml(labels.runtimeLog)}: <code>${escapeHtml(programmaticValidation.runtimeLog || "")}</code> · ${escapeHtml(labels.frameOutputDir)}: <code>${escapeHtml(programmaticValidation.frameOutputDir || "")}</code> · ${escapeHtml(labels.montage)}: <code>${escapeHtml(programmaticValidation.montage || "")}</code></p>` : ""}
    ${developerAdvice.length ? `<h2>${escapeHtml(labels.authorAdvice)}</h2>
    ${renderList(developerAdvice, (advice) => `<li class="advice"><strong><span class="pill ${escapeHtml(advice.severity || "")}">${escapeHtml(advice.severity || "advice")}</span> ${escapeHtml(advice.title || advice.code || "Review item")}${advice.target ? ` <span class="muted">[${escapeHtml(advice.target)}]</span>` : ""}</strong><span>${escapeHtml(advice.action || advice.explanation || "")}</span>${advice.recipe ? ` <span class="muted">Recipe: <code>${escapeHtml(advice.recipe)}</code></span>` : ""}${advice.text ? ` <span class="muted">Text: <code>${escapeHtml(advice.text)}</code></span>` : ""}${advice.path ? ` <span class="muted">Path: <code>${escapeHtml(advice.path)}</code></span>` : ""}${advice.node ? ` <span class="muted">Node: <code>${escapeHtml(advice.node)}</code></span>` : ""}${advice.metrics ? ` <span class="muted">Metrics: <code>${escapeHtml(JSON.stringify(advice.metrics))}</code></span>` : ""}</li>`, labels.none)}
    ` : ""}
    ${!isPackageValidation && hasRenderData && performanceSummary.model ? `<h2>${escapeHtml(labels.renderingPreflight)}</h2>
      <p>
        ${escapeHtml(labels.rating)}: <strong>${escapeHtml(performanceSummary.rating || "unknown")}</strong>
        · ${escapeHtml(labels.score)}: ${escapeHtml(performanceSummary.score || 0)}
        · ${escapeHtml(labels.maxToolTime)}: ${escapeHtml(performanceSummary.maxTotalPipelineUs || 0)} us
        · ${escapeHtml(labels.slowestStage)}: ${escapeHtml((performanceSummary.slowestMeasuredStage || {}).stage || "n/a")}
        · ${escapeHtml(labels.maxHeap)}: ${escapeHtml(performanceSummary.maxEstimatedHeapBytes || 0)} bytes
        · ${escapeHtml(labels.maxFramebuffer)}: ${escapeHtml(performanceSummary.maxFramebufferBytes || 0)} bytes
        · ${escapeHtml(labels.maxDisplayCommands)}: ${escapeHtml(performanceSummary.maxDisplayCommands || 0)}
      </p>
      ${renderList(performanceBottlenecks, (item) => `<li class="advice"><strong>${escapeHtml(item.title || item.code || "Performance bottleneck")}${item.target ? ` <span class="muted">[${escapeHtml(item.target)}]</span>` : ""}</strong>${item.metrics ? ` <span class="muted">Metrics: <code>${escapeHtml(JSON.stringify(item.metrics))}</code></span>` : ""}</li>`, labels.none)}
      ${renderList(performanceAdvice, (advice) => `<li class="advice"><strong><span class="pill ${escapeHtml(advice.severity || "")}">${escapeHtml(advice.severity || "advice")}</span> ${escapeHtml(advice.title || advice.code || "Performance item")}${advice.target ? ` <span class="muted">[${escapeHtml(advice.target)}]</span>` : ""}</strong><span>${escapeHtml(advice.action || advice.explanation || "")}</span>${advice.metrics ? ` <span class="muted">Metrics: <code>${escapeHtml(JSON.stringify(advice.metrics))}</code></span>` : ""}</li>`, labels.none)}
    ` : !isPackageValidation && performanceSummary.source === "package-preflight-estimate" ? `<h2>${escapeHtml(labels.staticEstimate)}</h2>
      <p class="muted">${escapeHtml(labels.staticEstimateNote)}</p>
      <p>${escapeHtml(labels.resourceBudget)}: ${escapeHtml(performanceSummary.resourceBudgetPercent || 0)}% · ${escapeHtml(labels.measuredFrameTime)}: ${escapeHtml(labels.notAvailable)}</p>` : ""}
    ${hasPipelineDiagnostics ? `<h2>${escapeHtml(labels.pipelineDiagnostics)}</h2>
      <p>
        ${escapeHtml(labels.total)}: ${escapeHtml(summary.total || 0)}
        · <span class="error">${escapeHtml(labels.errors)}: ${escapeHtml(summary.error || 0)}</span>
        · <span class="warning">${escapeHtml(labels.warnings)}: ${escapeHtml(summary.warning || 0)}</span>
        · <span class="info">${escapeHtml(labels.info)}: ${escapeHtml(summary.info || 0)}</span>
      </p>
      <p class="muted">
        ${escapeHtml(labels.dom)}: ${escapeHtml(pipelineStats.domNodes || 0)} ${escapeHtml(labels.nodes)} ·
        ${escapeHtml(labels.layout)}: ${escapeHtml(pipelineStats.layoutBoxes || 0)} ${escapeHtml(labels.boxes)} ·
        ${escapeHtml(labels.layers)}: ${escapeHtml(pipelineStats.layers || 0)} ·
        ${escapeHtml(labels.displayCommands)}: ${escapeHtml(pipelineStats.displayCommands || 0)} ·
        ${escapeHtml(labels.estimatedHeap)}: ${escapeHtml(pipelineStats.estimatedHeapBytes || 0)} bytes
      </p>
      ${renderList(pipelineDiagnostics, (diagnostic) => `<li><span class="pill ${escapeHtml(diagnostic.severity || "")}">${escapeHtml(diagnostic.severity || "diagnostic")}</span> <code>${escapeHtml(diagnostic.stage || "pipeline")}::${escapeHtml(diagnostic.code || "diagnostic")}</code> · ${escapeHtml(diagnostic.message || "")}${diagnostic.detail ? ` <span class="muted">(${escapeHtml(diagnostic.detail)})</span>` : ""}</li>`, labels.none)}
    ` : ""}
    <h2>${escapeHtml(labels.warningsSection)}</h2>
    ${renderList(warnings, (warning) => `<li>${escapeHtml(warning.message || warning.reason || JSON.stringify(warning))}</li>`, labels.none)}
    <h2>${escapeHtml(labels.resourcesSection)}</h2>
    <table><tr><th>${escapeHtml(labels.path)}</th><th>${escapeHtml(labels.kind)}</th><th>${escapeHtml(labels.bytes)}</th></tr>
    ${resources.map((resource) => `<tr><td><code>${escapeHtml(resource.path)}</code></td><td>${escapeHtml(resource.kind)}</td><td>${escapeHtml(resource.size)}</td></tr>`).join("")}
    </table>
    <h2>${escapeHtml(labels.references)}</h2>
    ${renderList(references, (reference) => `<li><code>${escapeHtml(reference.from)}</code> -> <code>${escapeHtml(reference.value)}</code> <span class="muted">${escapeHtml(reference.kind)} ${reference.packaged === false ? escapeHtml(labels.missing) : ""}</span></li>`, labels.none)}
  ` : `<p class="muted">${escapeHtml(labels.noReport)}</p>`}
</body>
</html>`;
}

function showReportPanel(context) {
  if (!reportPanel) {
    reportPanel = vscode.window.createWebviewPanel(
      "jellyframeReport",
      reportLabels().panelTitle,
      vscode.ViewColumn.Beside,
      { enableScripts: false }
    );
    reportPanel.onDidDispose(() => {
      reportPanel = undefined;
    }, null, context.subscriptions);
  }
  reportPanel.webview.html = reportHtml();
  reportPanel.reveal(vscode.ViewColumn.Beside);
}

function templateNames(context) {
  const root = path.join(repoRoot(context), "tools", "templates", "apps");
  if (!fs.existsSync(root)) {
    return [];
  }
  return fs.readdirSync(root).filter((name) => fs.statSync(path.join(root, name)).isDirectory()).sort();
}

function templateChoices(context) {
  const chinese = isChinese();
  const descriptions = chinese
    ? {
      blank: "最小 Hello world 包，适合从零开始。",
      calculator: "紧凑键盘与事件委托。",
      clock: "时间和状态仪表板。",
      timer: "本地状态与按钮交互。",
      weather: "数据卡片与包内图片。"
    }
    : {
      blank: "Minimal Hello world package for a clean start.",
      calculator: "Compact keypad and event delegation.",
      clock: "Time and status dashboard.",
      timer: "Local state and button interaction.",
      weather: "Data cards and package-local images."
    };
  return templateNames(context).map((name) => ({
    label: name,
    description: descriptions[name] || (chinese ? "官方 App 起始模板。" : "Official App starter template."),
    template: name
  }));
}

function suggestedAppId(directoryName) {
  const suffix = directoryName.toLowerCase()
    .replace(/[^a-z0-9_.-]+/g, "-")
    .replace(/^[_.-]+|[_.-]+$/g, "") || "app";
  return `org.example.${suffix}`;
}

function suggestedAppName(directoryName) {
  return directoryName
    .trim()
    .split(/[_.-]+/)
    .filter(Boolean)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(" ") || "App";
}

function directoryNameError(value, chinese) {
  const name = String(value || "").trim();
  if (!name) {
    return chinese ? "请输入新 App 的目录名称。" : "Enter a directory name for the new App.";
  }
  if (!DIRECTORY_NAME_PATTERN.test(name) || /[. ]$/.test(name)) {
    return chinese
      ? "目录名称不能包含路径分隔符、Windows 保留字符，且不能以句点或空格结束。"
      : "The directory name cannot contain path separators or Windows-reserved characters, or end with a dot or space.";
  }
  return undefined;
}

function appIdError(value, chinese) {
  if (APP_ID_PATTERN.test(String(value || "").trim())) {
    return undefined;
  }
  return chinese
    ? "App ID 必须以字母或数字开始，且仅可包含字母、数字、点、连字符或下划线。"
    : "App ID must start with a letter or digit and contain only letters, digits, dots, hyphens or underscores.";
}

async function newFromTemplate(context) {
  if (!requireAuthorSdk(context)) {
    return;
  }
  const chinese = isChinese();
  const picked = await vscode.window.showQuickPick(templateChoices(context), {
    placeHolder: chinese ? "选择 JellyFrame App 起始模板" : "Select a JellyFrame App starter template",
    ignoreFocusOut: true
  });
  if (!picked) {
    return;
  }
  const workspace = workspaceFolderPath() || repoRoot(context);
  const selectedParent = await vscode.window.showOpenDialog({
    defaultUri: vscode.Uri.file(workspace),
    canSelectFiles: false,
    canSelectFolders: true,
    canSelectMany: false,
    openLabel: chinese ? "选择新 App 的存放位置" : "Select a location for the new App"
  });
  if (!selectedParent || !selectedParent[0]) {
    return;
  }
  const directoryName = await vscode.window.showInputBox({
    prompt: chinese ? "新 App 目录名称" : "New App directory name",
    value: `${picked.template}-app`,
    placeHolder: chinese ? "仅输入目录名称，不输入路径" : "Directory name only, not a path",
    validateInput: (value) => directoryNameError(value, chinese),
    ignoreFocusOut: true
  });
  if (!directoryName) {
    return;
  }
  const normalizedDirectoryName = directoryName.trim();
  const output = path.join(selectedParent[0].fsPath, normalizedDirectoryName);
  if (fs.existsSync(output)) {
    vscode.window.showErrorMessage(chinese
      ? `目标目录已存在：${output}。请选择新的目录名称或位置。`
      : `The destination directory already exists: ${output}. Choose a new name or location.`);
    return;
  }
  const name = await vscode.window.showInputBox({
    prompt: chinese ? "App 显示名称" : "App display name",
    value: suggestedAppName(normalizedDirectoryName),
    validateInput: (value) => String(value || "").trim()
      ? undefined
      : (chinese ? "App 显示名称不能为空。" : "App display name cannot be empty."),
    ignoreFocusOut: true
  });
  if (!name) {
    return;
  }
  const defaultAppId = suggestedAppId(normalizedDirectoryName);
  const appIdMode = await vscode.window.showQuickPick([
    {
      label: chinese ? "使用建议的 App ID" : "Use suggested App ID",
      description: defaultAppId,
      value: defaultAppId
    },
    {
      label: chinese ? "指定 App ID" : "Specify App ID",
      description: chinese ? "适用于已有组织命名空间。" : "Use an existing organization namespace.",
      value: "custom"
    }
  ], {
    placeHolder: chinese ? "选择 App ID" : "Select an App ID",
    ignoreFocusOut: true
  });
  if (!appIdMode) {
    return;
  }
  const appId = appIdMode.value === "custom"
    ? await vscode.window.showInputBox({
      prompt: chinese ? "App ID" : "App ID",
      value: defaultAppId,
      placeHolder: "org.example.my-app",
      validateInput: (value) => appIdError(value, chinese),
      ignoreFocusOut: true
    })
    : appIdMode.value;
  if (!appId) {
    return;
  }
  const selectedTarget = await selectTarget(context, undefined, {
    presetOnly: true,
    purpose: chinese ? "选择新 App 的目标显示形态" : "Select a target display profile for the new App"
  });
  if (!selectedTarget) {
    return;
  }
  runCli(context, [
    "new",
    "--template",
    picked.template,
    "--output",
    output,
    "--id",
    appId.trim(),
    "--name",
    name.trim(),
    "--target",
    selectedTarget
  ]);
}

function activate(context) {
  capabilityDiagnostics = vscode.languages.createDiagnosticCollection("jellyframe");
  statusProvider = new JellyFrameStatusProvider(context);
  context.subscriptions.push(
    capabilityDiagnostics,
    statusProvider.changed,
    vscode.window.registerTreeDataProvider("jellyframe.status", statusProvider),
    vscode.window.onDidChangeActiveTextEditor(() => statusProvider?.refresh()),
    vscode.workspace.onDidChangeWorkspaceFolders(() => statusProvider?.refresh()),
    vscode.workspace.onDidSaveTextDocument(() => statusProvider?.refresh()),
    vscode.commands.registerCommand("jellyframe.validate", (resourceUri) => runPackageCommand(context, "validate", resourceUri)),
    vscode.commands.registerCommand("jellyframe.check", (resourceUri) => runPackageCommand(context, "check", resourceUri)),
    vscode.commands.registerCommand("jellyframe.preview", (resourceUri) => previewPackage(context, resourceUri)),
    vscode.commands.registerCommand("jellyframe.debug", (resourceUri) => debugApp(context, resourceUri)),
    vscode.commands.registerCommand("jellyframe.debugExternal", (resourceUri) => debugExternalApp(context, resourceUri)),
    vscode.commands.registerCommand("jellyframe.runFrameScript", (resourceUri) => runFrameScript(context, resourceUri)),
    vscode.commands.registerCommand("jellyframe.openCapture", () => openCapture(context)),
    vscode.commands.registerCommand("jellyframe.listBuilds", () => listBuilds(context)),
    vscode.commands.registerCommand("jellyframe.setupDesktopBuild", () => {
      const root = currentPackageRoot();
      const scripting = appRequiresScripting(root);
      return configureDesktopBuild(context, scripting);
    }),
    vscode.commands.registerCommand("jellyframe.manageAuthorEnvironment", () => manageAuthorEnvironment(context)),
    vscode.commands.registerCommand("jellyframe.package", (resourceUri) => runPackageCommand(context, "package", resourceUri)),
    vscode.commands.registerCommand("jellyframe.newFromTemplate", () => newFromTemplate(context)),
    vscode.commands.registerCommand("jellyframe.showReport", () => showReportPanel(context)),
    vscode.commands.registerCommand("jellyframe.showOutput", () => showOutputChannel()),
    vscode.commands.registerCommand("jellyframe.deviceDiscover", () => discoverDevice(context)),
    vscode.commands.registerCommand("jellyframe.deviceSelect", () => chooseDevice()),
    vscode.commands.registerCommand("jellyframe.deviceInfo", () => inspectDevice(context)),
    vscode.commands.registerCommand("jellyframe.deviceList", () => listDeviceApps(context)),
    vscode.commands.registerCommand("jellyframe.deviceDeploy", (resourceUri) => deployDeviceApp(context, resourceUri)),
    vscode.commands.registerCommand("jellyframe.deviceLaunch", () => runSelectedAppLifecycle(context, "launch", {
      chineseVerb: "启动"
    })),
    vscode.commands.registerCommand("jellyframe.deviceStop", () => runSelectedAppLifecycle(context, "stop", {
      chineseVerb: "停止"
    })),
    vscode.commands.registerCommand("jellyframe.deviceRemove", () => runSelectedAppLifecycle(context, "remove", {
      chineseVerb: "删除"
    })),
    vscode.commands.registerCommand("jellyframe.deviceRollback", () => runSelectedAppLifecycle(context, "rollback", {
      chineseVerb: "回滚",
      requireRollback: true
    })),
    vscode.commands.registerCommand("jellyframe.deviceLogs", () => runSelectedAppLifecycle(context, "logs", {
      chineseVerb: "读取日志"
    })),
    vscode.commands.registerCommand("jellyframe.deviceRecovery", () => inspectDeviceRecovery(context))
  );
}

function deactivate() {
  if (outputChannel) {
    outputChannel.dispose();
    outputChannel = undefined;
  }
  if (capabilityDiagnostics) {
    capabilityDiagnostics.dispose();
    capabilityDiagnostics = undefined;
  }
  if (statusProvider) {
    statusProvider.changed.dispose();
    statusProvider = undefined;
  }
  lastDeviceDiscovery = undefined;
  lastDeviceInfo = undefined;
  lastDeviceApps = undefined;
  lastDeviceEndpoint = undefined;
  lastDeviceLifecycle = undefined;
}

module.exports = {
  activate,
  deactivate
};
