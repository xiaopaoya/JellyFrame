"""Compare the desktop shell and CLI preflight renderer on manifest font selection."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def pixels(path):
    data = path.read_bytes()
    assert data[:2] == b'BM'
    offset = struct.unpack_from('<I', data, 10)[0]
    bits = struct.unpack_from('<H', data, 28)[0]
    assert bits in (24, 32)
    width, height = struct.unpack_from('<ii', data, 18)
    stride = ((width * bits + 31) // 32) * 4
    step = bits // 8
    rows = range(height - 1, -1, -1) if height > 0 else range(-height)
    return tuple(data[offset + y * stride + x * step:offset + y * stride + x * step + 3]
                 for y in rows for x in range(width))


def main():
    if len(sys.argv) != 4:
        raise SystemExit('usage: canonical_app_font_ink_tests.py SHELL_EXE PSEUDO_EXE EXACT_CORE_VERSION')
    repo = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        css = root / 'empty.css'
        css.touch()
        ink_counts = []
        for named in (False, True):
            app = root / ('named' if named else 'default')
            app.mkdir()
            fonts = []
            if named:
                (app / 'face.jffont2').write_bytes((repo / 'samples/apps/packages/jelly_font_policy/fonts/tiny_cn.jffont2').read_bytes())
                fonts = [{'family': 'Probe', 'source': '/face.jffont2'}]
            family = 'Probe' if named else 'system-ui'
            # The system catalog has a real bold face, while the named subset
            # contains only regular. At weight700 Core must select named400
            # before default700; equal output would conceal a family fallback.
            (app / 'index.html').write_text('<style>body{margin:0;background:#fff;color:#000}p{margin:0;font-size:16px;font-weight:700;font-family:' + family + '}</style><p>中小</p>', encoding='utf8')
            (app / 'jellyframe.app.json').write_text(json.dumps({
                'id': 'org.example.canonical.ink', 'entry': '/index.html',
                'runtime': {'minJellyFrame': '0.6.0', 'minRenderCore': sys.argv[3], 'script': 'none'},
                'fonts': fonts}), encoding='utf8')
            output = root / (family + '.bmp')
            result = subprocess.run([sys.argv[1], '--app', str(app), '--capture', str(output),
                '--viewport-width', '64', '--viewport-height', '40', '--use-app-fonts'], capture_output=True, text=True)
            assert result.returncode == 0, result.stdout + result.stderr
            assert 'text_backend=core-text' in result.stdout and 'diagnostics: 0' in result.stdout
            preflight_output = root / (family + '-preflight.bmp')
            report = root / (family + '.json')
            result = subprocess.run([sys.argv[2], str(app / 'index.html'), str(css), str(preflight_output),
                '64', '40', '--resource-root', str(app), '--package', '--diagnostics-json', str(report)],
                capture_output=True, text=True)
            assert result.returncode == 0, result.stdout + result.stderr
            assert json.loads(report.read_text(encoding='utf8'))['summary']['total'] == 0
            shell_pixels = pixels(output)
            assert pixels(preflight_output) == shell_pixels, f'{family}: preflight and shell differ'
            ink = sum(pixel != b'\xff\xff\xff' for pixel in shell_pixels)
            assert ink > 0
            print(f'{family}: matching canonical glyph ink={ink}')
            ink_counts.append(ink)
        assert ink_counts[0] != ink_counts[1], 'named regular ink must differ from default bold; do not hide catalog fallback'


if __name__ == '__main__':
    main()
