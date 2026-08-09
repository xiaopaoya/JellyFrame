import json
import hashlib
import struct
import subprocess
import sys
import tempfile
import shutil
import zlib
from pathlib import Path

JFAPP_HEADER_FORMAT = "<8sHHIIIIIIIIIII"
JFAPP_MAGIC = b"JFAPPV0\0"
JFAPP_HEADER_SIZE = struct.calcsize(JFAPP_HEADER_FORMAT)
REPO_ROOT = Path(__file__).resolve().parents[2]
STATIC_SVG_ICON_APP = REPO_ROOT / "tests" / "fixtures" / "apps" / "jelly_svg_icon"


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


def read_bmp_rgb(path: Path, x: int, y: int) -> tuple[int, int, int]:
    data = path.read_bytes()
    require(data[:2] == b"BM", f"{path} must be a BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits_per_pixel = struct.unpack_from("<H", data, 28)[0]
    require(width > x >= 0 and height > y >= 0 and bits_per_pixel == 24,
            f"{path} must be a 24-bit capture containing the requested pixel")
    stride = (width * 3 + 3) & ~3
    pixel = offset + (height - 1 - y) * stride + x * 3
    blue, green, red = data[pixel:pixel + 3]
    return red, green, blue


def write_jfapp(
    path: Path,
    app_id: str,
    version_code: int,
    version_name: str,
    entry: str,
    summary_text: str | None = None,
    capabilities: list[object] | None = None,
    network_allowed: bool | None = None,
) -> None:
    declared_capabilities = capabilities or []
    projected_network_allowed = (
        network_allowed if network_allowed is not None else "network.fetch" in declared_capabilities
    )
    summary = (summary_text.encode("utf-8") if summary_text is not None else json.dumps(
        {
            "id": app_id,
            "name": "Rollback Probe",
            "role": "app",
            "versionName": version_name,
            "versionCode": version_code,
            "entry": entry,
            "minJellyFrame": "0.5.0-dev",
            "script": "classic",
            "viewport": {"designWidth": 172, "designHeight": 320},
            "budgets": {"maxResourceBytes": 65536},
            "fonts": [],
            "targets": {"test": {"viewport": {"width": 172, "height": 320}, "output": "jfapp"}},
            "permissions": [],
            "capabilities": declared_capabilities,
            "computeJobsAllowed": False,
            "videoFrameAllowed": False,
            "networkAllowed": projected_network_allowed,
            "storageKvAllowed": False,
            "canvas2dAllowed": False,
            "audioPlaybackAllowed": False,
            "sensorAccelerometerAllowed": False,
            "sensorGyroscopeAllowed": False,
            "sensorHeartRateAllowed": False,
            "sensorAmbientLightAllowed": False,
            "locationPositionAllowed": False,
            "systemBatteryAllowed": False,
            "systemWeatherAllowed": False,
            "systemActivityAllowed": False,
            "backgroundServices": {},
        },
        separators=(",", ":"),
    ).encode("utf-8"))
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


def write_install_candidate(
    path: Path,
    bundle: Path,
    *,
    signature_status: str = "trusted",
    user_approval: bool = True,
    sha256: str | None = None,
) -> None:
    path.write_text(
        json.dumps(
            {
                "format": "jellyframe.install_candidate",
                "formatVersion": 0,
                "bundle": {
                    "path": bundle.name,
                    "sha256": sha256 or hashlib.sha256(bundle.read_bytes()).hexdigest(),
                },
                "signature": {"status": signature_status},
                "userApproval": user_approval,
            },
            indent=2,
        ),
        encoding="utf-8",
    )


def main() -> int:
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--scripting"):
        print("usage: win32_browser_cli_tests.py PATH_TO_EXE [--scripting]")
        return 2

    exe = Path(sys.argv[1])
    scripting_enabled = len(sys.argv) == 3
    require(exe.exists(), f"missing executable: {exe}")

    no_argument_result = run_case(exe, [])
    require(no_argument_result.returncode == 0, "no arguments must print help and exit successfully")
    require("No input app or capture options were provided." in no_argument_result.stdout,
            "no arguments must explain why the tool did not launch a page")
    require(any(f"usage: {name}" in no_argument_result.stdout
                for name in ("jellyframe_desktop_shell", "jellyframe_win32_browser")),
            "no arguments must print the short usage")

    help_result = run_case(exe, ["--help"])
    require(help_result.returncode == 0, "--help must exit successfully")
    require(any(f"usage: {name}" in help_result.stdout
                for name in ("jellyframe_desktop_shell", "jellyframe_win32_browser")),
            "--help must print usage")
    require("Frame script commands:" in help_result.stdout, "--help must document frame scripts")
    require("event FRAME:kind[:x:y[:delta]]" in help_result.stdout, "--help must document wheel delta")
    require("pointer-up, wheel, escape" in help_result.stdout, "--help must document deterministic Escape")
    require("event FRAME:time-ms:VALUE" in help_result.stdout, "--help must document host time injection")
    require("event FRAME battery PERCENT CHARGING" in help_result.stdout,
            "--help must document host battery injection")
    require("--keep-data" in help_result.stdout, "--help must document app data retention")
    require("--install-candidate" in help_result.stdout, "--help must document verified candidate installs")
    require("--delete-app-data" in help_result.stdout, "--help must document standalone app data deletion")
    require("--rollback-app" in help_result.stdout, "--help must document app rollback")
    require("--enable-app" in help_result.stdout, "--help must document app enable")
    require("--disable-app" in help_result.stdout, "--help must document app disable")
    require("--allow-downgrade" in help_result.stdout, "--help must document explicit downgrade installs")
    require("--app-runtime-jobs" in help_result.stdout, "--help must document app runtime queue override")
    require("--authorized-file-smoke" in help_result.stdout, "--help must document authorized file broker smoke")
    require("--system-survival-smoke" in help_result.stdout, "--help must document system survival smoke")

    unknown_option_result = run_case(exe, ["--not-a-real-option"])
    require(unknown_option_result.returncode != 0, "unknown options must fail")
    require("unknown option: --not-a-real-option" in unknown_option_result.stdout,
            "unknown options must name the invalid option")
    require("Use --help for usage." in unknown_option_result.stdout,
            "unknown options must point to --help")

    too_many_positional_result = run_case(exe, ["a.html", "a.css", "172", "320", "extra"])
    require(too_many_positional_result.returncode != 0, "too many positional arguments must fail")
    require("too many positional arguments" in too_many_positional_result.stdout,
            "too many positional arguments must explain the accepted shape")
    require("Use --help for usage." in too_many_positional_result.stdout,
            "positional argument errors must point to --help")

    with tempfile.TemporaryDirectory(prefix="jellyframe-scripted-pointer-drag-") as directory:
        root = Path(directory)
        app = root / "app"
        frames = root / "frames"
        app.mkdir()
        (app / "index.html").write_text(
            "<style>html,body{margin:0;width:160px;height:60px;background:#10151b;}"
            "input{width:140px;margin:10px;}</style>"
            "<input id='drag' type='range' min='0' max='100' value='0'>",
            encoding="utf-8",
        )
        (app / "jellyframe.app.json").write_text(
            json.dumps({
                "id": "org.jellyframe.pointer-drag-probe",
                "name": "Pointer Drag Probe",
                "role": "app",
                "versionName": "1.0.0",
                "versionCode": 1,
                "entry": "/index.html",
                "minJellyFrame": "0.5.0-dev",
                "script": "none",
                "viewport": {"designWidth": 160, "designHeight": 60},
            }),
            encoding="utf-8",
        )
        drag_result = run_case(
            exe,
            [
                "--app", str(app),
                "--capture-frames", str(frames),
                "--frame-count", "5",
                "--frame-event", "1:pointer-down:20:20",
                "--frame-event", "2:pointer-move:130:20",
                "--frame-event", "3:pointer-up:130:20",
            ],
        )
        require(drag_result.returncode == 0, "scripted pointer drag must capture")
        before_drag = frames / "frame_001.bmp"
        after_drag = frames / "frame_003.bmp"
        require(before_drag.is_file() and after_drag.is_file(),
                "scripted pointer drag must produce before/after frames")
        require(before_drag.read_bytes() != after_drag.read_bytes(),
                "pointer-move between pointer-down/up must update a range control")

    with tempfile.TemporaryDirectory(prefix="jellyframe-select-popup-") as directory:
        root = Path(directory)
        app = root / "app"
        frames = root / "frames"
        app.mkdir()
        (app / "index.html").write_text(
            "<style>html,body{margin:0;width:160px;height:100px;background:#10151b;}"
            "select{display:block;width:140px;height:24px;margin:10px;color:#10151b;background:#f8fafc;}"
            "</style><select id='choice'><option>One</option><option>Two</option></select>",
            encoding="utf-8",
        )
        (app / "jellyframe.app.json").write_text(
            json.dumps({
                "id": "org.jellyframe.select-popup-probe",
                "name": "Select Popup Probe",
                "role": "app",
                "versionName": "1.0.0",
                "versionCode": 1,
                "entry": "/index.html",
                "minJellyFrame": "0.5.0-dev",
                "script": "none",
                "viewport": {"designWidth": 160, "designHeight": 100},
            }),
            encoding="utf-8",
        )
        popup_result = run_case(
            exe,
            [
                "--app", str(app),
                "--capture-frames", str(frames),
                "--frame-count", "4",
                "--frame-event", "1:click:20:20",
                "--frame-event", "2:click:20:64",
            ],
        )
        require(popup_result.returncode == 0, "select popup capture must succeed")
        initial = frames / "frame_000.bmp"
        opened = frames / "frame_001.bmp"
        committed = frames / "frame_002.bmp"
        require(initial.is_file() and opened.is_file() and committed.is_file(),
                "select popup capture must produce initial, opened and committed frames")
        require(initial.read_bytes() != opened.read_bytes(),
                "select click must paint an option popup in forms.advanced builds")
        require(opened.read_bytes() != committed.read_bytes(),
                "select popup option click must close the popup and update the control")

    numeric_result = run_case(exe, ["--viewport-width", "nope"])
    require(numeric_result.returncode != 0, "invalid numeric option must fail")
    require("--viewport-width requires an integer" in numeric_result.stdout,
            "invalid numeric option must explain the failing option")

    event_result = run_case(exe, ["--frame-event", "2:wheel:x:120:-90"])
    require(event_result.returncode != 0, "invalid frame event must fail")
    require("wheel x, y and delta must be integers" in event_result.stdout,
            "invalid frame event must explain the failing field")

    with tempfile.TemporaryDirectory(prefix="jellyframe-background-image-") as directory:
        root = Path(directory)
        app = root / "app"
        capture = root / "background.bmp"
        shutil.copytree(Path("tools/templates/apps/weather"), app)
        (app / "index.html").write_text(
            "<link rel='stylesheet' href='styles/app.css'><body><main class='cover'></main></body>",
            encoding="utf-8",
        )
        (app / "styles" / "app.css").write_text(
            "body { margin: 0; }\n"
            ".cover { width: 96px; height: 64px; background-color: #102030; "
            "background-image: url('/assets/cloudy.bmp'); }\n",
            encoding="utf-8",
        )
        background_result = run_case(exe, ["--app", str(app), "--viewport-width", "120",
                                           "--viewport-height", "90", "--capture", str(capture)])
        require(background_result.returncode == 0, "package-style background image capture must pass")
        require("diagnostics: 0" in background_result.stdout,
                "supported package-style background image must not emit diagnostics")
        require(capture.is_file() and capture.stat().st_size > 54,
                "background image capture must produce a bitmap")

    with tempfile.TemporaryDirectory(prefix="jellyframe-nested-viewport-") as directory:
        app = Path(directory)
        (app / "index.html").write_text("<main>Viewport probe</main>", encoding="utf-8")
        (app / "jellyframe.app.json").write_text(
            json.dumps(
                {
                    "id": "org.jellyframe.viewport-probe",
                    "name": "Viewport Probe",
                    "role": "app",
                    "versionName": "1.0.0",
                    "versionCode": 1,
                    "entry": "/index.html",
                    "minJellyFrame": "0.5.0-dev",
                    "script": "none",
                    "viewport": {"designWidth": 172, "designHeight": 320},
                }
            ),
            encoding="utf-8",
        )
        capture = app / "viewport.bmp"
        viewport_result = run_case(exe, ["--app", str(app), "--capture", str(capture)])
        require(viewport_result.returncode == 0, "nested manifest viewport capture must pass")
        require("viewport_width=172" in viewport_result.stdout,
                "package manifest designWidth must select the default viewport")
        require("image=172x320" in viewport_result.stdout,
                "package manifest design dimensions must select the capture size")

    with tempfile.TemporaryDirectory(prefix="jellyframe-package-resource-link-") as directory:
        root = Path(directory)
        app = root / "app"
        outside = root / "outside.css"
        capture = root / "capture.bmp"
        shutil.copytree(Path("tools/templates/apps/weather"), app)
        outside.write_text("body { background: #ff0000; }", encoding="utf-8")
        linked = app / "styles" / "outside.css"
        try:
            linked.symlink_to(outside)
        except OSError as error:
            print(f"skipping source-package symlink check: {error}")
        else:
            (app / "index.html").write_text(
                "<link rel='stylesheet' href='styles/outside.css'><body><main>safe</main></body>",
                encoding="utf-8",
            )
            link_result = run_case(exe, ["--app", str(app), "--capture", str(capture)])
            require(link_result.returncode == 0, "rejected source-package symlink must recover into a capture")
            require("package-resource-rejected" in link_result.stdout,
                    "source-package symlink must be rejected by the app loader")

    with tempfile.TemporaryDirectory(prefix="jellyframe-static-svg-icon-") as directory:
        root = Path(directory)
        report = root / "report.json"
        capture = root / "static-svg.bmp"
        previewed = subprocess.run(
            [sys.executable, str(REPO_ROOT / "tools" / "jellyframe_cli.py"), "preview",
             "--root", str(STATIC_SVG_ICON_APP), "--report", str(report),
             "--output", str(capture), "--build-dir", str(exe.parent),
             "--width", "64", "--height", "64", "--rasterize-svg"],
            cwd=REPO_ROOT, check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(previewed.returncode == 0, f"static SVG preview must succeed: {previewed.stdout}")
        report_data = json.loads(report.read_text(encoding="utf-8"))
        svg_report = report_data.get("staticSvgRasterization", {})
        require(svg_report.get("rasterizedCount") == 1,
                "static SVG package report must record one generated icon")
        red, green, blue = read_bmp_rgb(capture, 32, 20)
        require(green > 160 and red < 80 and blue > 80,
                f"rasterized SVG icon must retain its green surface, got {(red, green, blue)}")

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
        blocked_downgrade = run_case(exe, ["--registry-store", str(store), "--install-bundle", str(first)])
        require(blocked_downgrade.returncode != 0, "win32 registry downgrade must be blocked by default")
        require("downgrade install is blocked" in blocked_downgrade.stdout,
                "win32 registry downgrade must explain the update policy")
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

        bundles = store / "bundles"
        staging = store / "staging"
        stale_stage = staging / "interrupted-update.staging"
        orphan_bundle = bundles / "interrupted-update.jfapp"
        stale_stage.write_bytes(b"partial")
        orphan_bundle.write_bytes(b"partial")
        recovery_result = run_case(exe, ["--registry-store", str(store), "--install-bundle", str(first)])
        require(recovery_result.returncode == 0, "registry recovery install must pass")
        require(not stale_stage.exists(), "registry recovery must remove stale staging files")
        require(not orphan_bundle.exists(), "registry recovery must remove orphaned bundles")
        registry = json.loads((store / "registry.json").read_text(encoding="utf-8"))
        app = registry["apps"][0]
        require((bundles / app["bundleFile"]).is_file(), "registry recovery must retain the current bundle")
        require((bundles / app["rollback"]["bundleFile"]).is_file(), "registry recovery must retain the rollback bundle")

        registry_file = store / "registry.json"
        registry_tmp = store / "registry.json.tmp"
        registry_file.replace(registry_tmp)
        tmp_recovery = run_case(exe, ["--registry-store", str(store), "--install-bundle", str(second)])
        require(tmp_recovery.returncode == 0, "native registry must recover a valid interrupted registry write")
        require(registry_file.is_file() and not registry_tmp.exists(),
                "native registry recovery must restore the valid temporary registry before mutation")
        registry = json.loads(registry_file.read_text(encoding="utf-8"))
        app = registry["apps"][0]
        preserved_bundles = {entry.name for entry in bundles.glob("*.jfapp")}
        registry_file.write_text("{ not valid JSON", encoding="utf-8")
        corrupt_result = run_case(exe, ["--registry-store", str(store), "--install-bundle", str(first)])
        require(corrupt_result.returncode != 0, "native registry must reject a corrupt registry before cleanup")
        require({entry.name for entry in bundles.glob("*.jfapp")} == preserved_bundles,
                "corrupt registry must not trigger orphan bundle deletion")
        registry_file.write_text(json.dumps(registry), encoding="utf-8")

    with tempfile.TemporaryDirectory(prefix="jellyframe-duplicate-summary-") as directory:
        bundle = Path(directory) / "duplicate.jfapp"
        write_jfapp(
            bundle,
            "org.jellyframe.duplicate-probe",
            1,
            "1.0.0",
            "/index.html",
            '{"id":"org.jellyframe.duplicate-probe","capabilities":["network.fetch"],"capabilities":[]}',
        )
        duplicate_result = run_case(exe, ["--registry-store", str(Path(directory) / "store"),
                                          "--install-bundle", str(bundle)])
        require(duplicate_result.returncode != 0, "native registry must reject duplicate manifest members")
        require("duplicate" in duplicate_result.stdout.lower(),
                "native duplicate manifest rejection must explain the malformed summary")

    with tempfile.TemporaryDirectory(prefix="jellyframe-summary-projection-drift-") as directory:
        bundle = Path(directory) / "projection-drift.jfapp"
        write_jfapp(bundle,
                    "org.jellyframe.projection-drift",
                    1,
                    "1.0.0",
                    "/index.html",
                    capabilities=["network.fetch"],
                    network_allowed=False)
        drift_result = run_case(exe, ["--registry-store", str(Path(directory) / "store"),
                                      "--install-bundle", str(bundle)])
        require(drift_result.returncode != 0,
                "native loader must reject a capability projection that disagrees with the summary arrays")
        require("networkAllowed does not match permissions/capabilities" in drift_result.stdout,
                "native capability projection rejection must name the inconsistent summary field")

    with tempfile.TemporaryDirectory(prefix="jellyframe-summary-array-shape-") as directory:
        bundle = Path(directory) / "array-shape.jfapp"
        write_jfapp(bundle,
                    "org.jellyframe.array-shape",
                    1,
                    "1.0.0",
                    "/index.html",
                    capabilities=[{"capability": "network.fetch"}],
                    network_allowed=True)
        shape_result = run_case(exe, ["--registry-store", str(Path(directory) / "store"),
                                      "--install-bundle", str(bundle)])
        require(shape_result.returncode != 0,
                "native loader must reject a non-string capability array member")
        require("capabilities must be a string array" in shape_result.stdout,
                "native array-shape rejection must name the malformed summary field")

    with tempfile.TemporaryDirectory(prefix="jellyframe-summary-entry-normalization-") as directory:
        bundle = Path(directory) / "entry-normalization.jfapp"
        write_jfapp(bundle,
                    "org.jellyframe.entry-normalization",
                    1,
                    "1.0.0",
                    "/pages/../index.html")
        entry_result = run_case(exe, ["--registry-store", str(Path(directory) / "store"),
                                      "--install-bundle", str(bundle)])
        require(entry_result.returncode != 0,
                "native loader must reject a non-normalized summary entry path")
        require("entry must be a normalized absolute app path" in entry_result.stdout,
                "native entry rejection must name the malformed summary field")

    with tempfile.TemporaryDirectory(prefix="jellyframe-install-candidate-") as directory:
        root = Path(directory)
        store = root / "store"
        bundle = root / "candidate.jfapp"
        candidate = root / "candidate.json"
        write_jfapp(bundle, "org.jellyframe.candidate-probe", 1, "1.0.0", "/index.html")
        write_install_candidate(candidate, bundle)
        installed = run_case(exe, ["--registry-store", str(store), "--install-candidate", str(candidate)])
        require(installed.returncode == 0, f"trusted approved install candidate must pass: {installed.stdout}")
        require("installed-candidate org.jellyframe.candidate-probe 1.0.0" in installed.stdout,
                "candidate install must report the installed app")

        update_bundle = root / "candidate-v2.jfapp"
        update_candidate = root / "candidate-v2.json"
        write_jfapp(update_bundle, "org.jellyframe.candidate-probe", 2, "2.0.0", "/v2.html")
        write_install_candidate(update_candidate, update_bundle)
        updated = run_case(exe, ["--registry-store", str(store), "--install-candidate", str(update_candidate)])
        require(updated.returncode == 0, "verified candidate update must pass")
        registry = json.loads((store / "registry.json").read_text(encoding="utf-8"))
        app = registry["apps"][0]
        require(app["versionCode"] == 2 and app["rollback"]["versionCode"] == 1,
                "candidate update must retain the previous bundle for rollback")

        untrusted = root / "untrusted.json"
        write_install_candidate(untrusted, bundle, signature_status="untrusted")
        result = run_case(exe, ["--registry-store", str(store), "--install-candidate", str(untrusted)])
        require(result.returncode != 0 and "signature-not-trusted" in result.stdout,
                "untrusted install candidate must be rejected")

        not_approved = root / "not-approved.json"
        write_install_candidate(not_approved, bundle, user_approval=False)
        result = run_case(exe, ["--registry-store", str(store), "--install-candidate", str(not_approved)])
        require(result.returncode != 0 and "user-approval-required" in result.stdout,
                "unapproved install candidate must be rejected")

        bad_hash = root / "bad-hash.json"
        write_install_candidate(bad_hash, bundle, sha256="0" * 64)
        result = run_case(exe, ["--registry-store", str(store), "--install-candidate", str(bad_hash)])
        require(result.returncode != 0 and "bundle-hash-mismatch" in result.stdout,
                "candidate with a mismatched bundle hash must be rejected")

        trailing_data = root / "trailing-data.json"
        write_install_candidate(trailing_data, bundle)
        trailing_data.write_text(trailing_data.read_text(encoding="utf-8") + "\nnot-json\n", encoding="utf-8")
        result = run_case(exe, ["--registry-store", str(store), "--install-candidate", str(trailing_data)])
        require(result.returncode != 0 and "invalid install candidate" in result.stdout,
                "candidate JSON with trailing data must be rejected")
        registry = json.loads((store / "registry.json").read_text(encoding="utf-8"))
        require(registry["apps"][0]["versionCode"] == 2,
                "rejected candidates must leave the currently launchable version unchanged")

        conflicting = run_case(
            exe,
            ["--registry-store", str(store), "--install-bundle", str(bundle), "--install-candidate", str(candidate)],
        )
        require(conflicting.returncode != 0 and "cannot be used together" in conflicting.stdout,
                "raw bundle and verified candidate install inputs must be mutually exclusive")

    with tempfile.TemporaryDirectory(prefix="jellyframe-launcher-action-") as directory:
        root = Path(directory)
        store = root / "store"
        bundle = root / "launcher-action.jfapp"
        frames = root / "frames"
        app_id = "org.jellyframe.examples.audio_smoke"
        write_jfapp(bundle, app_id, 1, "1.0.0", "/index.html")
        install_result = run_case(exe, ["--registry-store", str(store), "--install-bundle", str(bundle)])
        require(install_result.returncode == 0, "launcher action fixture must install")
        app_data = store / "data" / app_id
        app_data.mkdir(parents=True)
        (app_data / "state.txt").write_text("keep", encoding="utf-8")
        confirmation_result = run_case(
            exe,
            [
                "--registry-store", str(store),
                "--launcher-app", "samples/apps/system/sample_launcher",
                "--capture-frames", str(frames),
                "--frame-count", "2",
                "--frame-event", "1:click:270:300",
            ],
        )
        require(confirmation_result.returncode == 0, "launcher destructive-action confirmation must capture")
        require("diagnostics: 0" in confirmation_result.stdout,
                "launcher confirmation fixture must remain inside the documented rendering subset")
        registry = json.loads((store / "registry.json").read_text(encoding="utf-8"))
        require(len(registry["apps"]) == 1, "first destructive-action click must not remove the installed bundle")
        require(app_data.is_dir(), "first destructive-action click must preserve app-private data")

        cancel_result = run_case(
            exe,
            [
                "--registry-store", str(store),
                "--launcher-app", "samples/apps/system/sample_launcher",
                "--capture-frames", str(frames),
                "--frame-count", "3",
                "--frame-event", "1:click:270:300",
                "--frame-event", "2:click:270:227",
            ],
        )
        require(cancel_result.returncode == 0, "launcher destructive-action cancellation must capture")
        registry = json.loads((store / "registry.json").read_text(encoding="utf-8"))
        require(len(registry["apps"]) == 1, "cancelled destructive action must preserve the installed bundle")
        require(app_data.is_dir(), "cancelled destructive action must preserve app-private data")

        action_result = run_case(
            exe,
            [
                "--registry-store", str(store),
                "--launcher-app", "samples/apps/system/sample_launcher",
                "--capture-frames", str(frames),
                "--frame-count", "3",
                "--frame-event", "1:click:270:300",
                "--frame-event", "2:click:100:227",
            ],
        )
        require(action_result.returncode == 0, "launcher confirmed remove-keep-data action must capture")
        require("diagnostics: 0" in action_result.stdout,
                "launcher confirmed action fixture must remain inside the documented rendering subset")
        registry = json.loads((store / "registry.json").read_text(encoding="utf-8"))
        require(not registry["apps"], "launcher remove-keep-data action must remove the installed bundle")
        require(app_data.is_dir(), "launcher remove-keep-data action must preserve app-private data")

    with tempfile.TemporaryDirectory(prefix="jellyframe-failed-app-") as directory:
        store = Path(directory) / "store"
        bad_bundle = Path(directory) / "bad-entry.jfapp"
        bad_capture = Path(directory) / "bad-entry.bmp"
        write_jfapp(bad_bundle, "org.jellyframe.bad-entry", 1, "1.0.0", "/missing.html")
        install_bad = run_case(exe, ["--registry-store", str(store), "--install-bundle", str(bad_bundle)])
        require(install_bad.returncode == 0, "win32 bad-entry install must pass before launch validation")
        launch_bad = run_case(
            exe,
            [
                "--registry-store",
                str(store),
                "--launch-app",
                "org.jellyframe.bad-entry",
                "--capture",
                str(bad_capture),
            ],
        )
        require(launch_bad.returncode != 0, "win32 bad-entry launch must fail")
        registry = json.loads((store / "registry.json").read_text(encoding="utf-8"))
        app = registry["apps"][0]
        require(app["status"] == "failed", "win32 bad-entry launch must mark app failed")
        require(app["enabled"] is False, "win32 bad-entry launch must disable failed app")
        require(app["failure"]["reason"] == "load-failed", "win32 bad-entry launch must record failure reason")

    if scripting_enabled:
        with tempfile.TemporaryDirectory(prefix="jellyframe-dialog-modal-") as directory:
            captures = Path(directory) / "captures"
            dialog_result = run_case(
                exe,
                [
                    "--app",
                    "tests/fixtures/apps/jelly_dialog_modal",
                    "--frame-script",
                    "tests/fixtures/apps/jelly_dialog_modal/capture_modal_escape.jfcapture",
                    "--capture-frames",
                    str(captures),
                ],
            )
            require(dialog_result.returncode == 0, "dialog frame-script fixture must pass")
            require("diagnostics: 0" in dialog_result.stdout,
                    "dialog frame-script fixture must not emit diagnostics")
            open_pixel = read_bmp_rgb(captures / "frame_001.bmp", 16, 70)
            closed_pixel = read_bmp_rgb(captures / "frame_002.bmp", 16, 70)
            require(open_pixel[0] > 180 and open_pixel[1] < 130,
                    "showModal must render the confirmation surface before Escape")
            require(closed_pixel[0] < 30 and closed_pixel[1] < 40 and closed_pixel[2] < 60,
                    "Escape must cancel and close the confirmation surface")

        with tempfile.TemporaryDirectory(prefix="jellyframe-watchdog-app-") as directory:
            store = Path(directory) / "store"
            package_root = Path("tests/fixtures/apps/jelly_watchdog_smoke")
            bundle = Path(directory) / "watchdog.jfapp"
            package_report = Path(directory) / "watchdog.report.json"
            montage = Path(directory) / "watchdog.bmp"
            packaged_root = Path(directory) / "package"
            shutil.copytree(package_root, packaged_root)
            manifest_path = packaged_root / "jellyframe.app.json"
            manifest_text = manifest_path.read_text(encoding="utf-8")
            target_block = (
                '  "targets": {\n'
                '    "round-300": {\n'
                '      "viewport": {"width": 300, "height": 300, "shape": "round"},\n'
                '      "fontProfile": "tiny-plus-symbols",\n'
                '      "output": "jfapp"\n'
                '    }\n'
                '  },\n'
            )
            marker = '  "capabilities": []\n'
            require(marker in manifest_text, "watchdog fixture manifest must have an empty capability list")
            manifest_path.write_text(manifest_text.replace(marker, target_block + marker), encoding="utf-8")
            package_result = subprocess.run(
                [
                    sys.executable,
                    "tools/package_app.py",
                    "--root",
                    str(packaged_root),
                    "--output-bundle",
                    str(bundle),
                    "--report",
                    str(package_report),
                ],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            require(package_result.returncode == 0, "watchdog fixture must package for registry recovery")
            install_result = run_case(exe, ["--registry-store", str(store), "--install-bundle", str(bundle)])
            require(install_result.returncode == 0, "watchdog fixture must install for registry recovery")
            watchdog_result = run_case(
                exe,
                [
                    "--registry-store",
                    str(store),
                    "--launch-app",
                    "org.jellyframe.tests.watchdog_smoke",
                    "--frame-script",
                    str(package_root / "capture_watchdog.jfcapture"),
                    "--capture-montage",
                    str(montage),
                ],
            )
            require(watchdog_result.returncode == 0, "watchdog recovery must return to the system shell")
            require("script_watchdog_recovery reason=script-watchdog" in watchdog_result.stdout,
                    "watchdog recovery must report its stable reason")
            registry = json.loads((store / "registry.json").read_text(encoding="utf-8"))
            app = registry["apps"][0]
            require(app["status"] == "failed", "watchdog recovery must mark an installed app failed")
            require(app["enabled"] is False, "watchdog recovery must disable the failed app")
            require(app["failure"]["reason"] == "script-watchdog",
                    "watchdog recovery must preserve the stable failure reason")

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
