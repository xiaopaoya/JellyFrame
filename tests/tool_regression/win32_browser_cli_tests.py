import json
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

JFAPP_HEADER_FORMAT = "<8sHHIIIIIIIIIII"
JFAPP_MAGIC = b"JFAPPV0\0"
JFAPP_HEADER_SIZE = struct.calcsize(JFAPP_HEADER_FORMAT)


def run_case(exe: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(exe), *args],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def write_jfapp(path: Path, app_id: str, version_code: int, version_name: str, entry: str) -> None:
    summary = json.dumps(
        {
            "id": app_id,
            "name": "Rollback Probe",
            "versionName": version_name,
            "versionCode": version_code,
            "entry": entry,
            "script": "classic",
        },
        separators=(",", ":"),
    ).encode("utf-8")
    summary_offset = JFAPP_HEADER_SIZE
    summary_size = len(summary)
    index_offset = summary_offset + summary_size
    header = struct.pack(
        JFAPP_HEADER_FORMAT,
        JFAPP_MAGIC,
        JFAPP_HEADER_SIZE,
        0,
        0,
        summary_offset,
        summary_size,
        index_offset,
        0,
        index_offset,
        0,
        index_offset,
        0,
        0,
        0,
    )
    bundle = bytearray(header + summary)
    crc = zlib.crc32(bundle) & 0xFFFFFFFF
    struct.pack_into("<I", bundle, 48, crc)
    path.write_bytes(bundle)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: win32_browser_cli_tests.py PATH_TO_EXE")
        return 2

    exe = Path(sys.argv[1])
    require(exe.exists(), f"missing executable: {exe}")

    help_result = run_case(exe, ["--help"])
    require(help_result.returncode == 0, "--help must exit successfully")
    require("usage: jellyframe_win32_browser" in help_result.stdout, "--help must print usage")
    require("Frame script commands:" in help_result.stdout, "--help must document frame scripts")
    require("event FRAME:kind[:x:y[:delta]]" in help_result.stdout, "--help must document wheel delta")
    require("event FRAME:time-ms:VALUE" in help_result.stdout, "--help must document host time injection")
    require("event FRAME battery PERCENT CHARGING" in help_result.stdout,
            "--help must document host battery injection")
    require("--keep-data" in help_result.stdout, "--help must document app data retention")
    require("--delete-app-data" in help_result.stdout, "--help must document standalone app data deletion")
    require("--rollback-app" in help_result.stdout, "--help must document app rollback")
    require("--enable-app" in help_result.stdout, "--help must document app enable")
    require("--disable-app" in help_result.stdout, "--help must document app disable")
    require("--app-runtime-jobs" in help_result.stdout, "--help must document app runtime queue override")
    require("--authorized-file-smoke" in help_result.stdout, "--help must document authorized file broker smoke")
    require("--system-survival-smoke" in help_result.stdout, "--help must document system survival smoke")

    numeric_result = run_case(exe, ["--viewport-width", "nope"])
    require(numeric_result.returncode != 0, "invalid numeric option must fail")
    require("--viewport-width requires an integer" in numeric_result.stdout,
            "invalid numeric option must explain the failing option")

    event_result = run_case(exe, ["--frame-event", "2:wheel:x:120:-90"])
    require(event_result.returncode != 0, "invalid frame event must fail")
    require("wheel x, y and delta must be integers" in event_result.stdout,
            "invalid frame event must explain the failing field")

    time_event_result = run_case(exe, ["--frame-event", "2:time-ms:nope"])
    require(time_event_result.returncode != 0, "invalid time event must fail")
    require("time-ms value must be a non-negative integer" in time_event_result.stdout,
            "invalid time event must explain the failing field")

    weather_event_result = run_case(exe, ["--frame-event", "2:weather:213:windy"])
    require(weather_event_result.returncode != 0, "invalid weather event must fail")
    require("weather condition must be one of" in weather_event_result.stdout,
            "invalid weather event must explain the failing field")

    service_status_result = run_case(
        exe,
        [
            "--app",
            "samples/apps/packages/jelly_service_status",
            "--frame-script",
            "samples/apps/packages/jelly_service_status/capture_system_events.jfcapture",
        ],
    )
    require(service_status_result.returncode == 0, "service status frame script must pass")
    require(
        "host_data battery=yes percent=88 charging=1 weather=rain temp_x10=213 activity=yes steps=6400 minutes=32"
        in service_status_result.stdout,
        "service status frame script must report filtered host data",
    )

    with tempfile.TemporaryDirectory(prefix="jellyframe-authorized-file-") as directory:
        file_result = run_case(exe, ["--authorized-file-smoke", directory])
        require(file_result.returncode == 0, "authorized file smoke must pass")
        require("authorized_file_smoke denied=user-approval-required unchanged=yes" in file_result.stdout,
                "authorized file smoke must reject unapproved writes")
        require("authorized_file_smoke traversal=traversal-rejected unchanged=yes" in file_result.stdout,
                "authorized file smoke must reject traversal")
        require("authorized_file_smoke commit=accepted changed=yes" in file_result.stdout,
                "authorized file smoke must commit authorized write")
        require("authorized_file_smoke rollback=operation-unsupported preserved=yes" in file_result.stdout,
                "authorized file smoke must preserve previous file on failed staged write")
        require("authorized_file_smoke delete_denied=capability-denied delete_allowed=accepted" in file_result.stdout,
                "authorized file smoke must gate manage operations")
        require("authorized_file_smoke result=ok" in file_result.stdout,
                "authorized file smoke must report success")

    with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
        store = Path(directory) / "store"
        first = Path(directory) / "rollback-v1.jfapp"
        second = Path(directory) / "rollback-v2.jfapp"
        write_jfapp(first, "org.jellyframe.rollback-probe", 1, "1.0.0", "/v1.html")
        write_jfapp(second, "org.jellyframe.rollback-probe", 2, "2.0.0", "/v2.html")
        install_first = run_case(exe, ["--registry-store", str(store), "--install-bundle", str(first)])
        require(install_first.returncode == 0, "win32 registry install v1 must pass")
        install_second = run_case(exe, ["--registry-store", str(store), "--install-bundle", str(second)])
        require(install_second.returncode == 0, "win32 registry install v2 must pass")
        disable_result = run_case(exe, ["--registry-store", str(store), "--disable-app", "org.jellyframe.rollback-probe"])
        require(disable_result.returncode == 0, "win32 registry disable must pass")
        disabled_launch = run_case(exe, ["--registry-store", str(store), "--launch-app", "org.jellyframe.rollback-probe"])
        require(disabled_launch.returncode != 0, "disabled app launch must fail before opening UI")
        require("app is not launchable" in disabled_launch.stdout, "disabled app launch must explain launchability")
        enable_result = run_case(exe, ["--registry-store", str(store), "--enable-app", "org.jellyframe.rollback-probe"])
        require(enable_result.returncode == 0, "win32 registry enable must pass")
        rollback_result = run_case(exe, ["--registry-store", str(store), "--rollback-app", "org.jellyframe.rollback-probe"])
        require(rollback_result.returncode == 0, "win32 registry rollback must pass")
        require("rolled-back org.jellyframe.rollback-probe 1.0.0" in rollback_result.stdout,
                "win32 registry rollback must report restored version")
        registry = json.loads((store / "registry.json").read_text(encoding="utf-8"))
        app = registry["apps"][0]
        require(app["enabled"] is True, "win32 registry enable must restore enabled flag")
        require(app["status"] == "installed", "win32 registry enable must restore installed status")
        require(app["versionCode"] == 1, "win32 registry rollback must restore version code")
        require(app["entry"] == "/v1.html", "win32 registry rollback must restore entry point")
        require(app["rollback"]["versionCode"] == 2, "win32 registry rollback must preserve current version as rollback")
        require(app["rollback"]["entry"] == "/v2.html", "win32 registry rollback must preserve current entry as rollback")

    survival_result = run_case(exe, ["--system-survival-smoke", "12"])
    require(survival_result.returncode == 0, "system survival smoke must pass")
    require("system_survival_smoke cycles=12 recovered=12" in survival_result.stdout,
            "system survival smoke must recover every bad-app cycle")
    require("stale_completions=12" in survival_result.stdout,
            "system survival smoke must filter stale completions")
    require("shell_events=12" in survival_result.stdout,
            "system survival smoke must keep launcher events alive")
    require("system_survival_smoke result=ok" in survival_result.stdout,
            "system survival smoke must report success")

    print("win32 browser cli tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
