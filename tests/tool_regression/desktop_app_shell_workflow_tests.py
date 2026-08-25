import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PACKAGE_SCRIPT = REPO_ROOT / "tools" / "package_app.py"
LAUNCHER_APP = REPO_ROOT / "samples" / "apps" / "system" / "sample_launcher"
APP_ID = "org.jellyframe.workflow.probe"
APP_NAME = "Workflow Probe"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_case(exe: Path, args: list[str], log_path: Path) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(exe), *args],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(result.stdout, encoding="utf-8")
    return result


def write_source_package(root: Path, version_code: int, version_name: str, body_label: str) -> None:
    root.mkdir(parents=True, exist_ok=True)
    manifest = {
        "format": "jellyframe.app",
        "formatVersion": 0,
        "id": APP_ID,
        "name": APP_NAME,
        "version": {"name": version_name, "code": version_code},
        "entry": "/index.html",
        "runtime": {"minJellyFrame": "0.6.0", "minRenderCore": "0.6.1", "script": "classic"},
        "viewport": {"designWidth": 300, "designHeight": 300, "shape": "round"},
        "budgets": {
            "maxResourceBytes": 65536,
            "maxDomNodes": 128,
            "maxCssRules": 64,
            "maxDisplayCommands": 128,
            "maxTimers": 0,
            "maxEventListeners": 0,
        },
        "capabilities": [],
        "targets": {
            "round-300": {
                "viewport": {"width": 300, "height": 300, "shape": "round"},
                "fontProfile": "tiny-plus-symbols",
                "output": "jfapp",
                "gate": {
                    "policy": "reject",
                    "minViewport": {"width": 300, "height": 300},
                    "allowScroll": False,
                    "allowHorizontalOverflow": False,
                    "maxWarnings": 0,
                    "maxErrors": 0,
                },
            }
        },
    }
    (root / "jellyframe.app.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (root / "index.html").write_text(
        "<!doctype html>\n"
        "<html><head><meta charset='utf-8'><title>Workflow Probe</title>"
        "<link rel='stylesheet' href='styles/app.css'></head>"
        f"<body><main class='screen'><h1>{APP_NAME}</h1><p>{body_label}</p></main></body></html>\n",
        encoding="utf-8",
    )
    styles = root / "styles"
    styles.mkdir(exist_ok=True)
    (styles / "app.css").write_text(
        "body { margin: 0; width: 100%; height: 100%; padding-top: 118px; box-sizing: border-box; "
        "background-color: #09121d; color: #ecf7fb; text-align: center; }\n"
        ".screen { text-align: center; }\n"
        "h1 { margin: 0; font-size: 28px; }\n"
        "p { margin: 8px 0 0; color: #afc1cb; font-size: 15px; }\n",
        encoding="utf-8",
    )


def package_bundle(source_root: Path, bundle_path: Path, report_path: Path) -> None:
    command = [
        sys.executable,
        str(PACKAGE_SCRIPT),
        "--root",
        str(source_root),
        "--report",
        str(report_path),
        "--output-bundle",
        str(bundle_path),
        "--target",
        "round-300",
    ]
    completed = subprocess.run(
        command,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    report_path.with_suffix(report_path.suffix + ".log").write_text(completed.stdout, encoding="utf-8")
    require(completed.returncode == 0, f"package failed for {source_root}: {completed.stdout}")
    require(bundle_path.is_file(), f"expected packaged bundle: {bundle_path}")


def write_install_candidate(path: Path, bundle: Path) -> None:
    import hashlib

    data = {
        "format": "jellyframe.install_candidate",
        "formatVersion": 0,
        "bundle": {
            "path": bundle.name,
            "sha256": hashlib.sha256(bundle.read_bytes()).hexdigest(),
            "size": bundle.stat().st_size,
        },
        "signature": {
            "status": "trusted",
            "scheme": "host-test",
            "publisher": "JellyFrame Tests",
        },
        "userApproval": True,
        "download": {
            "source": "https://example.invalid/workflow-probe.jfapp",
            "transport": "host-owned",
        },
    }
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def load_registry(store: Path) -> dict:
    return json.loads((store / "registry.json").read_text(encoding="utf-8"))


def record_step(steps: list[dict], name: str, result: subprocess.CompletedProcess[str] | None = None, **extra: object) -> None:
    step = {"name": name, "passed": True}
    if result is not None:
        step["exitCode"] = result.returncode
    step.update(extra)
    steps.append(step)


def run_primary_workflow(exe: Path, root: Path, steps: list[dict]) -> None:
    packages = root / "packages"
    logs = root / "logs"
    captures = root / "captures"
    store = root / "store"
    packages.mkdir(parents=True, exist_ok=True)
    logs.mkdir(parents=True, exist_ok=True)
    captures.mkdir(parents=True, exist_ok=True)
    v1_source = packages / "source-v1"
    v2_source = packages / "source-v2"
    v1_bundle = packages / "workflow-v1.jfapp"
    v2_bundle = packages / "workflow-v2.jfapp"
    v1_candidate = packages / "workflow-v1.install.json"
    v2_candidate = packages / "workflow-v2.install.json"

    write_source_package(v1_source, 1, "1.0.0", "Version 1")
    write_source_package(v2_source, 2, "2.0.0", "Version 2")
    package_bundle(v1_source, v1_bundle, packages / "workflow-v1.package.report.json")
    package_bundle(v2_source, v2_bundle, packages / "workflow-v2.package.report.json")
    write_install_candidate(v1_candidate, v1_bundle)
    write_install_candidate(v2_candidate, v2_bundle)
    record_step(steps, "package-v1")
    record_step(steps, "package-v2")

    install_v1 = run_case(exe, ["--registry-store", str(store), "--install-bundle", str(v1_bundle)], logs / "install-v1.log")
    require(install_v1.returncode == 0, f"install v1 must pass: {install_v1.stdout}")
    registry = load_registry(store)
    require(registry["apps"][0]["versionCode"] == 1, "install v1 must record version 1")
    record_step(steps, "install-v1", install_v1)

    launch_v1_capture = captures / "launch-v1.ppm"
    launch_v1 = run_case(
        exe,
        ["--registry-store", str(store), "--launch-app", APP_ID, "--capture", str(launch_v1_capture)],
        logs / "launch-v1.log",
    )
    require(launch_v1.returncode == 0, f"launch v1 must pass: {launch_v1.stdout}")
    require(launch_v1_capture.is_file(), "launch v1 must emit a capture")
    record_step(steps, "launch-v1", launch_v1, capture=launch_v1_capture.name)

    install_v2 = run_case(exe, ["--registry-store", str(store), "--install-candidate", str(v2_candidate)], logs / "install-v2.log")
    require(install_v2.returncode == 0, f"candidate update must pass: {install_v2.stdout}")
    registry = load_registry(store)
    app = registry["apps"][0]
    require(app["versionCode"] == 2, "candidate update must install version 2")
    require(app["rollback"]["versionCode"] == 1, "candidate update must retain version 1 for rollback")
    record_step(steps, "install-candidate-v2", install_v2)

    launcher_capture = captures / "launcher-v2.ppm"
    launcher_v2 = run_case(
        exe,
        ["--registry-store", str(store), "--launcher-app", str(LAUNCHER_APP), "--capture", str(launcher_capture)],
        logs / "launcher-v2.log",
    )
    require(launcher_v2.returncode == 0, f"launcher capture after update must pass: {launcher_v2.stdout}")
    require("diagnostics: 0" in launcher_v2.stdout, "launcher capture after update must stay inside the documented subset")
    record_step(steps, "launcher-capture-v2", launcher_v2, capture=launcher_capture.name)

    blocked_downgrade = run_case(exe, ["--registry-store", str(store), "--install-bundle", str(v1_bundle)], logs / "blocked-downgrade.log")
    require(blocked_downgrade.returncode != 0, "downgrade install must be blocked by default")
    require("downgrade install is blocked" in blocked_downgrade.stdout, "blocked downgrade must explain the policy")
    record_step(steps, "block-downgrade", blocked_downgrade, expected="rejection")

    disable_result = run_case(exe, ["--registry-store", str(store), "--disable-app", APP_ID], logs / "disable.log")
    require(disable_result.returncode == 0, "disable must pass")
    registry = load_registry(store)
    require(registry["apps"][0]["enabled"] is False, "disable must mark the app disabled")
    record_step(steps, "disable-app", disable_result)

    blocked_launch = run_case(exe, ["--registry-store", str(store), "--launch-app", APP_ID], logs / "blocked-launch.log")
    require(blocked_launch.returncode != 0, "disabled app launch must fail")
    require("app is not launchable" in blocked_launch.stdout, "disabled launch must explain launchability")
    record_step(steps, "block-disabled-launch", blocked_launch, expected="rejection")

    enable_result = run_case(exe, ["--registry-store", str(store), "--enable-app", APP_ID], logs / "enable.log")
    require(enable_result.returncode == 0, "enable must pass")
    registry = load_registry(store)
    require(registry["apps"][0]["enabled"] is True, "enable must restore the enabled flag")
    record_step(steps, "enable-app", enable_result)

    rollback_result = run_case(exe, ["--registry-store", str(store), "--rollback-app", APP_ID], logs / "rollback.log")
    require(rollback_result.returncode == 0, f"rollback must pass: {rollback_result.stdout}")
    registry = load_registry(store)
    app = registry["apps"][0]
    require(app["versionCode"] == 1, "rollback must restore version 1")
    require(app["rollback"]["versionCode"] == 2, "rollback must retain version 2 as the rollback candidate")
    record_step(steps, "rollback-app", rollback_result)

    launch_rollback_capture = captures / "launch-rollback.ppm"
    launch_rollback = run_case(
        exe,
        ["--registry-store", str(store), "--launch-app", APP_ID, "--capture", str(launch_rollback_capture)],
        logs / "launch-rollback.log",
    )
    require(launch_rollback.returncode == 0, f"launch after rollback must pass: {launch_rollback.stdout}")
    require(launch_rollback_capture.is_file(), "launch after rollback must emit a capture")
    record_step(steps, "launch-rollback", launch_rollback, capture=launch_rollback_capture.name)


def run_launcher_keep_data_flow(exe: Path, root: Path, bundle: Path, steps: list[dict]) -> None:
    store = root / "store"
    logs = root / "logs"
    captures = root / "captures"
    logs.mkdir(parents=True, exist_ok=True)
    captures.mkdir(parents=True, exist_ok=True)
    install_result = run_case(exe, ["--registry-store", str(store), "--install-bundle", str(bundle)], logs / "install.log")
    require(install_result.returncode == 0, f"keep-data fixture install must pass: {install_result.stdout}")
    app_data = store / "data" / APP_ID
    app_data.mkdir(parents=True, exist_ok=True)
    (app_data / "state.txt").write_text("keep", encoding="utf-8")
    action_result = run_case(
        exe,
        [
            "--registry-store",
            str(store),
            "--launcher-app",
            str(LAUNCHER_APP),
            "--capture-frames",
            str(captures),
            "--frame-count",
            "3",
            "--frame-event",
            "1:click:270:300",
            "--frame-event",
            "2:click:100:227",
        ],
        logs / "launcher-remove-keep-data.log",
    )
    require(action_result.returncode == 0, f"launcher keep-data removal must pass: {action_result.stdout}")
    require("diagnostics: 0" in action_result.stdout, "launcher keep-data flow must stay inside the documented subset")
    registry = load_registry(store)
    require(not registry["apps"], "launcher keep-data flow must remove the installed bundle")
    require(app_data.is_dir(), "launcher keep-data flow must preserve private data")
    require((captures / "frame_002.bmp").is_file(), "launcher keep-data flow must emit confirmation frames")
    record_step(steps, "launcher-remove-keep-data", action_result, captureDir="captures")


def run_remove_delete_data_flow(exe: Path, root: Path, bundle: Path, steps: list[dict]) -> None:
    store = root / "store"
    logs = root / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    install_result = run_case(exe, ["--registry-store", str(store), "--install-bundle", str(bundle)], logs / "install.log")
    require(install_result.returncode == 0, f"delete-data fixture install must pass: {install_result.stdout}")
    app_data = store / "data" / APP_ID
    app_data.mkdir(parents=True, exist_ok=True)
    (app_data / "state.txt").write_text("delete", encoding="utf-8")
    remove_result = run_case(exe, ["--registry-store", str(store), "--remove-app", APP_ID], logs / "remove.log")
    require(remove_result.returncode == 0, f"remove with data deletion must pass: {remove_result.stdout}")
    registry = load_registry(store)
    require(not registry["apps"], "remove with data deletion must remove the installed bundle")
    require(not app_data.exists(), "remove with data deletion must remove private data")
    record_step(steps, "remove-delete-data", remove_result)


def write_report(path: Path, steps: list[dict]) -> None:
    passed = sum(1 for step in steps if step.get("passed"))
    report = {
        "format": "jellyframe.desktop_app_shell_workflow",
        "formatVersion": 0,
        "status": "passed" if passed == len(steps) else "failed",
        "summary": {"total": len(steps), "passed": passed, "failed": len(steps) - passed},
        "steps": steps,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    if len(sys.argv) not in (2, 4) or (len(sys.argv) == 4 and sys.argv[2] != "--output-dir"):
        print("usage: desktop_app_shell_workflow_tests.py PATH_TO_EXE [--output-dir DIR]")
        return 2
    exe = Path(sys.argv[1])
    require(exe.is_file(), f"missing executable: {exe}")
    output_dir = Path(sys.argv[3]) if len(sys.argv) == 4 else None

    with tempfile.TemporaryDirectory(prefix="jellyframe-app-shell-workflow-") as directory:
        root = Path(directory)
        evidence_root = output_dir if output_dir is not None else root / "evidence"
        if output_dir is not None:
            shutil.rmtree(output_dir, ignore_errors=True)
            output_dir.mkdir(parents=True, exist_ok=True)
        steps: list[dict] = []
        try:
            run_primary_workflow(exe, evidence_root / "primary", steps)
            primary_bundle = evidence_root / "primary" / "packages" / "workflow-v1.jfapp"
            run_launcher_keep_data_flow(exe, evidence_root / "launcher_keep_data", primary_bundle, steps)
            run_remove_delete_data_flow(exe, evidence_root / "remove_delete_data", primary_bundle, steps)
            write_report(evidence_root / "desktop_app_shell_workflow.report.json", steps)
        except Exception as error:  # pragma: no cover - direct script failure path
            write_report(evidence_root / "desktop_app_shell_workflow.report.json", steps)
            print(f"desktop app-shell workflow failed: {error}")
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
