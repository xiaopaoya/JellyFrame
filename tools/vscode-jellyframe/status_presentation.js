const path = require("path");

function desktopBuildPresentation(buildDirectory, chinese) {
  if (!buildDirectory) {
    return {
      summary: chinese ? "未找到兼容构建" : "No compatible build",
      profile: chinese ? "未配置" : "Not configured",
      output: chinese ? "未找到输出目录" : "No output directory",
      scripting: chinese ? "未知" : "Unknown"
    };
  }

  const profileDirectory = path.dirname(buildDirectory);
  const profileId = path.basename(profileDirectory);
  const configuration = path.basename(buildDirectory);
  const scripting = /(?:^|-)scripting(?:-|$)/.test(profileId);
  const runtime = scripting
    ? (chinese ? "脚本桌面壳" : "Script-enabled desktop shell")
    : (chinese ? "标准桌面壳" : "Standard desktop shell");
  return {
    summary: `${runtime} · ${configuration}`,
    profile: `${profileId} · ${configuration}`,
    output: buildDirectory,
    scripting: scripting
      ? (chinese ? "已启用" : "Enabled")
      : (chinese ? "未启用" : "Not enabled")
  };
}

module.exports = { desktopBuildPresentation };
