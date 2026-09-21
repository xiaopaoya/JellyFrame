"""Measure author-process startup without changing the installed SDK or App."""

import argparse
import json
import os
import platform
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path


# Runs inside the selected SDK interpreter, including older released SDKs.
CLI_BOOTSTRAP = """
import json, runpy, subprocess, sys, time
call = subprocess.call
def measured_call(command, *args, **kwargs):
    start = time.perf_counter()
    result = call(command, *args, **kwargs)
    print('JF_STARTUP_STEP ' + json.dumps({'executable': command[0],
          'argument': command[1] if len(command) > 1 else '',
          'elapsedMs': round((time.perf_counter()-start)*1000, 3), 'exitCode': result}),
          file=sys.stderr, flush=True)
    return result
subprocess.call = measured_call
sys.argv = sys.argv[1:]
runpy.run_path(sys.argv[0], run_name='__main__')
"""


def measure(command, cwd, log, timeout=90, first_frame=False):
    start = time.perf_counter()
    events = queue.Queue(maxsize=256)
    stopped = threading.Event()
    result = {"command": command, "firstOutputMs": None, "firstFrameMs": None,
              "timedOut": False, "steps": [], "logTruncated": False}
    process = subprocess.Popen(command, cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, errors="replace",
                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    result["spawnReturnedMs"] = round((time.perf_counter() - start) * 1000, 3)

    def read():
        while not stopped.is_set():
            line = process.stdout.readline(65536)
            event = (time.perf_counter(), line or None)
            while not stopped.is_set():
                try:
                    events.put(event, timeout=0.1)
                    break
                except queue.Full:
                    continue
            if not line:
                return

    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    deadline = start + timeout
    try:
        with log.open("w", encoding="utf-8") as output:
            logged = 0
            while True:
                if time.perf_counter() >= deadline:
                    result["timedOut"] = True
                    break
                try:
                    timestamp, line = events.get(timeout=max(0.001, deadline - time.perf_counter()))
                except queue.Empty:
                    result["timedOut"] = True
                    break
                if line is None:
                    break
                elapsed = round((timestamp - start) * 1000, 3)
                if result["firstOutputMs"] is None:
                    result["firstOutputMs"] = elapsed
                if logged < 4 * 1024 * 1024:
                    output.write(f"[{elapsed:.3f} ms] {line}")
                    logged += len(line)
                else:
                    result["logTruncated"] = True
                if line.startswith("JF_STARTUP_STEP "):
                    result["steps"].append(json.loads(line[len("JF_STARTUP_STEP "):]))
                if first_frame and line.startswith("JF_FRAME\t") and result["firstFrameMs"] is None:
                    result["firstFrameMs"] = elapsed
                    process.stdin.write("quit\n")
                    process.stdin.flush()
                    deadline = time.perf_counter() + 5
            if not result["timedOut"]:
                try:
                    process.wait(timeout=max(0.001, deadline - time.perf_counter()))
                except subprocess.TimeoutExpired:
                    result["timedOut"] = True
    finally:
        stopped.set()
        if process.poll() is None:
            if os.name == "nt":
                subprocess.run([str(Path(os.environ["SystemRoot"]) / "System32/taskkill.exe"),
                                "/pid", str(process.pid), "/t", "/f"], capture_output=True, timeout=15)
            else:
                process.kill()
            process.wait(timeout=15)
        reader.join(timeout=5)
        process.stdin.close()
        process.stdout.close()
    result.update(elapsedMs=round((time.perf_counter() - start) * 1000, 3),
                  exitCode=process.returncode, log=log.name)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--repeats", type=int, choices=range(1, 6), default=3)
    parser.add_argument("--target", help="Same optional target preset selected in the extension.")
    args = parser.parse_args()
    sdk, app, output = args.sdk.resolve(), args.app.resolve(), args.output.resolve()
    manifest = json.loads((app / "jellyframe.app.json").read_text(encoding="utf-8-sig"))
    python = sdk / "runtime/python/python.exe"
    cli = sdk / "tools/jellyframe_cli.py"
    scripted = manifest.get("runtime", {}).get("script", manifest.get("script", "none")) not in (None, "", "none")
    profile = "desktop-scripting-release" if scripted else "desktop-release"
    native = sdk / "build" / profile / "Release"
    shell = native / "jellyframe_desktop_shell.exe"
    for file in (python, cli, shell):
        if not file.is_file():
            parser.error(f"missing SDK file: {file}")
    output.mkdir(parents=True, exist_ok=False)
    summary = {"format": "jellyframe.author.startup-probe.v0", "sdk": str(sdk), "app": str(app),
               "platform": platform.platform(), "profile": profile, "runs": [],
               "limitations": ["Host wall time, not rendering CPU time or device FPS.",
                                "First run is only first observed, not guaranteed cold cache.",
                                "Security scanning is not disabled or attributed by this probe."]}
    for repeat in range(args.repeats):
        directory = output / f"run-{repeat + 1}"
        directory.mkdir()
        common = ["--root", str(app), "--build-dir", str(sdk / "build/desktop-release/Release")]
        if args.target:
            common.extend(["--target", args.target])
        tests = [
            ("python-start", [str(python), "-c", "print('ready', flush=True)"]),
            ("shell-start", [str(shell), "--help"]),
            ("validate", [str(python), "-c", CLI_BOOTSTRAP, str(cli), "validate", *common,
                          "--report", str(directory / "validate.json")]),
            ("preview", [str(python), "-c", CLI_BOOTSTRAP, str(cli), "preview", *common,
                         "--report", str(directory / "preview.json"), "--output", str(directory / "preview.bmp")]),
            ("debug-first-frame", [str(python), str(sdk / "tools/debug/jellyframe_debug.py"),
                                   "--build-dir", str(native), "--app", str(app), "--vscode-debug",
                                   "--vscode-frame-dir", str(directory / "frames"),
                                   "--runtime-log", str(directory / "runtime.log"), "--wait"]),
        ]
        for name, command in tests:
            print(f"[{repeat + 1}/{args.repeats}] {name} ...", flush=True)
            result = measure(command, output, directory / f"{name}.log", first_frame=name == "debug-first-frame")
            result.update(name=name, repeat=repeat + 1)
            summary["runs"].append(result)
            (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
            print(f"  elapsed={result['elapsedMs']} ms firstFrame={result['firstFrameMs']} "
                  f"exit={result['exitCode']} timeout={result['timedOut']}", flush=True)
    print(f"Report: {output / 'summary.json'}", flush=True)


if __name__ == "__main__":
    main()
