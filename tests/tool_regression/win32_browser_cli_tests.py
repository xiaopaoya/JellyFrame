import subprocess
import sys
import tempfile
from pathlib import Path


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
    require("--keep-data" in help_result.stdout, "--help must document app data retention")
    require("--delete-app-data" in help_result.stdout, "--help must document standalone app data deletion")
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
