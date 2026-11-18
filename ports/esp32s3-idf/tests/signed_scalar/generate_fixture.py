"""Create a format0 classic-script fixture with the public host-only test key."""
from pathlib import Path
import struct
import sys
import tempfile

from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))
import package_app
import sign_app_package


def make_fixture(output, core_version=None):
    manifest = package_app.read_manifest(ROOT / "samples/apps/packages/jelly_font_policy")
    if core_version is not None:
        manifest["runtime"]["minRenderCore"] = core_version
    manifest.update(id="org.jellyframe.signed-test", name="Signed Scalar", fonts=[],
                    viewport={"designWidth": 172, "designHeight": 320, "shape": "rect"})
    manifest["runtime"]["script"] = "classic"
    manifest["targets"] = {"ws147": {"viewport": {"width": 172, "height": 320, "shape": "rect"},
                                     "output": "jfapp"}}
    manifest = package_app.validate_manifest(manifest)
    row = bytes((0x24, 0x88, 0xE1, 0))
    bmp = struct.pack("<2sIHHI", b"BM", 54 + len(row), 0, 0, 54)
    bmp += struct.pack("<IiiHHIIiiII", 40, 1, 1, 1, 24, 0, len(row), 0, 0, 0, 0) + row
    assets = {
        "/index.html": b'<html><head><link rel="stylesheet" href="/app.css"></head><body>'
                       b'<div id="title">CORE TEXT</div><img src="/mark.bmp">'
                       b'<div id="next">NEXT 0</div><script src="/app.js"></script></body></html>',
        "/app.css": b'*{margin:0;padding:0;font-weight:400;font-size:16px;font-family:sans-serif}'
                    b'body{background:#ffffff;color:#111111}'
                    b'#title{height:24px;line-height:24px}img{display:block;width:20px;height:20px}'
                    b'#next{width:80px;height:28px;line-height:28px;background-color:#2255bb;color:#ffffff}',
        "/app.js": b"var next=document.getElementById('next');next.addEventListener('click',function(){"
                   b"next.textContent='NEXT 1';next.style.backgroundColor='#127744';});",
        "/mark.bmp": bmp,
    }
    with tempfile.TemporaryDirectory(dir=output.parent) as temp:
        folder = Path(temp)
        resources = []
        for url, data in assets.items():
            path = folder / url[1:]
            path.write_bytes(data)
            resources.append({"path": url, "file": path, "kind": package_app.resource_kind(path)})
        bundle = folder / "scalar.jfapp"
        package_app.write_jfapp_bundle(bundle, manifest, resources)
        encoded = sign_app_package.sign_bundle(bundle.read_bytes(), ec.derive_private_key(1, ec.SECP256R1()),
                                               bytes(range(0x10, 0x20)))
        signed = folder / "scalar.jfsg"
        signed.write_bytes(encoded)
        signed.replace(output)


if __name__ == "__main__":
    make_fixture(Path(sys.argv[1]), *sys.argv[2:])
