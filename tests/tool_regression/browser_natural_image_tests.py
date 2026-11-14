#!/usr/bin/env python3
"""Normal Win32 entry-point oracle; requires a natural-image-capable Core."""
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
from package_image_fixture_tests import read_bmp_pixels


def main():
    exe, evidence = Path(sys.argv[1]), Path(sys.argv[2])
    evidence.mkdir(parents=True, exist_ok=True)
    app = evidence / "six-images"
    app.mkdir(exist_ok=True)
    manifest = json.loads((ROOT / "samples/apps/packages/watch_weather/jellyframe.app.json").read_text())
    manifest.update(id="org.example.natural", name="Natural image host oracle")
    manifest["runtime"]["minRenderCore"] = "0.9.0-dev"
    manifest.pop("targets", None)
    manifest.pop("requiresFeatures", None)
    (app / "jellyframe.app.json").write_text(json.dumps(manifest))
    # Distinct URLs force six separate decode completions. Solid red dimensions
    # and a green following block establish geometry without a font dependency.
    for i in range(9):
        pixels = bytes((0, 0, 255)) * 10 + bytes(2)
        payload = pixels * 10
        header = b"BM" + struct.pack("<IHHI", 54 + len(payload), 0, 0, 54)
        header += struct.pack("<IiiHHIIiiII", 40, 10, 10, 1, 24, 0, len(payload), 0, 0, 0, 0)
        (app / f"image{i}.bmp").write_bytes(header + payload)
    (app / "index.html").write_text('<html><head><link rel="stylesheet" href="/style.css"></head><body>' +
        ''.join(f'<img id="image{i}" src="/image{i}.bmp">' for i in range(6)) +
        '<div id="following"></div>' + ''.join(f'<img class="metadata" src="/image{i}.bmp">' for i in range(6, 9)) + '</body></html>')
    (app / "style.css").write_text('html,body{margin:0;padding:0;background:#ffffff}img{display:block}.metadata{width:0px;height:auto}#following{width:10px;height:10px;background:#00ff00}')

    def run(args, log):
        result = subprocess.run([str(exe), "--app", str(app), "--viewport-width", "120", "--viewport-height", "100", *args],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (evidence / log).write_text(result.stdout)
        if result.returncode:
            raise AssertionError(result.stdout)

    run(["--capture", str(evidence / "static.bmp")], "static.log")
    for name, extra in [("normal", []), ("full", ["--force-full-repaint"])]:
        run(["--capture-frames", str(evidence / name), "--frame-count", "16", "--frame-step-ms", "33", *extra], name + ".log")
    log = (evidence / "normal.log").read_text()
    assert "host_completion_handlers image=9" in log, log
    assert "handles=9/" in log, "metadata-only handles must remain protected above cache budget8"
    normal = sorted((evidence / "normal").glob("*.bmp"))
    full = sorted((evidence / "full").glob("*.bmp"))
    assert len(normal) == len(full) == 16, (normal, full)
    for a, b in zip(normal, full):
        assert a.read_bytes() == b.read_bytes(), f"incremental/full mismatch: {a}"

    def geometry(path):
        w, h, pixels = read_bmp_pixels(path)
        assert (w, h) == (120, 100)
        for y in range(60):
            assert pixels[y * w + 5] == (255, 0, 0), (path, y, pixels[y * w + 5])
        for y in range(60, 70):
            assert pixels[y * w + 5] == (0, 255, 0), (path, y, pixels[y * w + 5])
        assert pixels[70 * w + 5] == (255, 255, 255)
        return pixels

    assert geometry(evidence / "static.bmp") == geometry(normal[-1]), "static/normal final mismatch"
    # Switch one source after the initial decode drain. Predeclare the new URL
    # on a zero-width metadata consumer, matching normal package fixture discovery.
    data = bytes((255, 0, 0)) * 20 * 5
    header = b"BM" + struct.pack("<IHHI", 54 + len(data), 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, 20, 5, 1, 24, 0, len(data), 0, 0, 0, 0)
    (app / "replacement.bmp").write_bytes(header + data)
    html = (app / "index.html").read_text().replace('</body>', '<img class="metadata" src="/replacement.bmp"></body>')
    (app / "index.html").write_text(html)
    script = evidence / "replace.js"
    script.write_text('setTimeout(function(){document.getElementById("image0").setAttribute("src","/replacement.bmp");},350);')
    for name, extra in [("replace", []), ("replace-full", ["--force-full-repaint"])]:
        run(["--script", str(script), "--capture-frames", str(evidence / name), "--frame-count", "24", "--frame-step-ms", "33", *extra], name + ".log")
    a = sorted((evidence / "replace").glob("*.bmp"))
    b = sorted((evidence / "replace-full").glob("*.bmp"))
    assert len(a) == len(b) == 24
    assert all(x.read_bytes() == y.read_bytes() for x, y in zip(a, b)), "src replacement incremental drift"
    w, h, pixels = read_bmp_pixels(a[-1])
    assert pixels[2 * w + 15] == (0, 0, 255), "replacement natural width20 not applied"
    assert pixels[55 * w + 5] == (0, 255, 0), "replacement natural height5 did not move sibling to y55"
    assert pixels[65 * w + 5] == (255, 255, 255)

    # Exercise both one-auto-axis ratio directions, authored definite axes,
    # and an unresolved source through the same Browser layout/paint path.
    sizing = evidence / "sizing"
    sizing.mkdir(exist_ok=True)
    data = bytes((0, 0, 255)) * 10 + bytes(2)
    payload = data * 5
    header = b"BM" + struct.pack("<IHHI", 54 + len(payload), 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, 10, 5, 1, 24, 0, len(payload), 0, 0, 0, 0)
    (sizing / "ten-by-five.bmp").write_bytes(header + payload)
    (sizing / "jellyframe.app.json").write_text(json.dumps(manifest))
    (sizing / "index.html").write_text(
        '<html><head><link rel="stylesheet" href="/style.css"></head><body>'
        '<img id="width-ratio" src="/ten-by-five.bmp">'
        '<img id="height-ratio" src="/ten-by-five.bmp">'
        '<img id="definite" src="/ten-by-five.bmp">'
        '<img id="unresolved" src="/missing.bmp">'
        '<div id="following"></div></body></html>')
    (sizing / "style.css").write_text(
        'html,body{margin:0;padding:0;background:#ffffff}img{display:block}'
        '#width-ratio{width:20px;height:auto}'
        '#height-ratio{width:auto;height:20px}'
        '#definite{width:12px;height:6px}'
        '#following{width:10px;height:10px;background:#00ff00}')
    result = subprocess.run(
        [str(exe), "--app", str(sizing), "--viewport-width", "100", "--viewport-height", "60",
         "--capture", str(evidence / "sizing.bmp")],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (evidence / "sizing.log").write_text(result.stdout)
    if result.returncode:
        raise AssertionError(result.stdout)
    w, h, pixels = read_bmp_pixels(evidence / "sizing.bmp")
    assert (w, h) == (100, 60)
    assert pixels[9 * w + 19] == (255, 0, 0), "width definite/height auto ratio not painted at 20x10"
    assert pixels[29 * w + 39] == (255, 0, 0), "height definite/width auto ratio not painted at 40x20"
    assert pixels[35 * w + 11] == (255, 0, 0), "both definite axes not painted at 12x6"
    assert all(pixels[y * w + 5] == (0, 255, 0) for y in range(36, 46)), \
        "unresolved image changed following content position"
    # An unused background declaration registers a package fixture without
    # requesting it. The changed src now goes Unknown -> completion -> Known.
    (app / "index.html").write_text(html.replace('<img class="metadata" src="/replacement.bmp">', '<div style="height:240px;width:1px"></div>'))
    css = (app / "style.css").read_text()
    (app / "style.css").write_text(css + '.future{background-image:url("/replacement.bmp")}body{min-height:240px}')
    for name, extra in [("unknown-scroll", []), ("unknown-scroll-full", ["--force-full-repaint"])]:
        run(["--script", str(script), "--capture-frames", str(evidence / name), "--frame-count", "30", "--frame-step-ms", "33", "--frame-event", "24:wheel:5:5:-120", *extra], name + ".log")
    a = sorted((evidence / "unknown-scroll").glob("*.bmp"))
    b = sorted((evidence / "unknown-scroll-full").glob("*.bmp"))
    assert len(a) == len(b) == 30
    assert all(x.read_bytes() == y.read_bytes() for x, y in zip(a, b)), "unknown source or scroll incremental drift"
    w, h, pixels = read_bmp_pixels(a[22])
    assert pixels[2 * w + 15] == (0, 0, 255)
    assert pixels[55 * w + 5] == (0, 255, 0)
    log = (evidence / "unknown-scroll.log").read_text()
    assert "host_completion_handlers image=10" in log
    print("PASS src replacement known metadata; sibling y55; 24 incremental/full frames")
    print("PASS six painted + three metadata-only images under cache pressure; following sibling y=60; static/normal pixels; 16 incremental/full frames")


if __name__ == "__main__":
    main()


