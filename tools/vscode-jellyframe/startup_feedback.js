function startupMessage(elapsedMs, chinese = false) {
  const seconds = Math.max(0, Math.floor(elapsedMs / 1000));
  if (seconds >= 10) {
    return chinese
      ? `启动较慢，仍在等待（${seconds} 秒）`
      : `Startup is taking longer; still waiting (${seconds}s)`;
  }
  return chinese ? `正在加载（${seconds} 秒）` : `Loading (${seconds}s)`;
}

async function withStartupProgress(vscode, title, task, chinese = false, timers = globalThis) {
  return vscode.window.withProgress({
    location: vscode.ProgressLocation.Notification, title, cancellable: false
  }, async (progress) => {
    const start = Date.now();
    progress.report({ message: startupMessage(0, chinese) });
    const timer = timers.setInterval(() => {
      progress.report({ message: startupMessage(Date.now() - start, chinese) });
    }, 1000);
    try {
      return await task();
    } finally {
      timers.clearInterval(timer);
    }
  });
}

module.exports = { startupMessage, withStartupProgress };
