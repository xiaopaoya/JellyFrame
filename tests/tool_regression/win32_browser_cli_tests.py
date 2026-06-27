import subprocess
import sys
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

    numeric_result = run_case(exe, ["--viewport-width", "nope"])
    require(numeric_result.returncode != 0, "invalid numeric option must fail")
    require("--viewport-width requires an integer" in numeric_result.stdout,
            "invalid numeric option must explain the failing option")

    event_result = run_case(exe, ["--frame-event", "2:wheel:x:120:-90"])
    require(event_result.returncode != 0, "invalid frame event must fail")
    require("wheel x, y and delta must be integers" in event_result.stdout,
            "invalid frame event must explain the failing field")

    print("win32 browser cli tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
