"""Run with SHELL_EXE EXACT_CORE_VERSION; never relax pre-release admission."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def ink(path):
    data = path.read_bytes()
    offset = struct.unpack_from('<I', data, 10)[0]
    bits = struct.unpack_from('<H', data, 28)[0]
    assert bits in (24, 32)
    width, height = struct.unpack_from('<ii', data, 18)
    stride = ((width * bits + 31) // 32) * 4
    step = bits // 8
    return sum(data[offset + y * stride + x * step:offset + y * stride + x * step + 3] != b'\xff\xff\xff'
               for y in range(abs(height)) for x in range(width))


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: canonical_app_font_ink_tests.py SHELL_EXE EXACT_CORE_VERSION')
    repo = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for named in (False, True):
            app = root / ('named' if named else 'default')
            app.mkdir()
            fonts = []
            if named:
                (app / 'face.jffont2').write_bytes((repo / 'samples/apps/packages/jelly_font_policy/fonts/tiny_cn.jffont2').read_bytes())
                fonts = [{'family': 'Probe', 'source': '/face.jffont2'}]
            family = 'Probe' if named else 'system-ui'
            (app / 'index.html').write_text('<style>body{margin:0;background:#fff;color:#000}p{margin:0;font-size:16px;font-family:' + family + '}</style><p>中小</p>', encoding='utf8')
            (app / 'jellyframe.app.json').write_text(json.dumps({
                'id': 'org.example.canonical.ink', 'entry': '/index.html',
                'runtime': {'minJellyFrame': '0.6.0', 'minRenderCore': sys.argv[2], 'script': 'none'},
                'fonts': fonts}), encoding='utf8')
            output = root / (family + '.bmp')
            result = subprocess.run([sys.argv[1], '--app', str(app), '--capture', str(output),
                '--viewport-width', '64', '--viewport-height', '40', '--use-app-fonts'], capture_output=True, text=True)
            assert result.returncode == 0, result.stdout + result.stderr
            assert 'text_backend=core-text' in result.stdout and 'diagnostics: 0' in result.stdout
            assert ink(output) > 0
            print(f'{family}: canonical glyph ink={ink(output)}')


if __name__ == '__main__':
    main()
