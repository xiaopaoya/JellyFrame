"""Exercise actual native entry points; scalar Core text is not contextual JFSP."""
import argparse
import json
from pathlib import Path
import subprocess
import struct
import tempfile

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('desktop', type=Path)
parser.add_argument('pseudo', type=Path)
parser.add_argument('--core-version', required=True)
parser.add_argument('--core-abi', required=True, type=int)
parser.add_argument('--output', type=Path)
args = parser.parse_args()


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def run(label, command, success=True):
    result = subprocess.run([str(x) for x in command], capture_output=True,
                            text=True, timeout=30)
    (root / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    require(result.returncode == (0 if success else 1),
            f'{label}: exit {result.returncode}: {result.stdout}{result.stderr}')
    return result.stdout + result.stderr


def pixels(path):
    data = path.read_bytes()
    header, data = data.split(b'\n', 1)
    dimensions, data = data.split(b'\n', 1)
    maximum, data = data.split(b'\n', 1)
    require(header == b'P6' and maximum == b'255', 'Expected RGB PPM')
    width, height = map(int, dimensions.split())
    require(len(data) == width * height * 3, 'Truncated RGB pixels')
    return width, height, data


def check():
    html = root / 'index.html'
    css = root / 'app.css'
    html.write_text('<!doctype html><body><div id="label">ABC office 123</div></body>', encoding='utf-8')
    css.write_text('body{margin:0;background:#fff;color:#000}#label{font-size:24px}', encoding='utf-8')
    output = root / 'default.ppm'
    explicit = root / 'explicit.ppm'
    pseudo = root / 'pseudo.ppm'
    diagnostics = root / 'diagnostics.json'
    log = run('default', [args.desktop, '--capture', output, html, css, 172, 320])
    require('text_backend=core-text' in log, 'Native default silently selected platform text')
    run('explicit', [args.desktop, '--use-app-fonts', '--capture', explicit, html, css, 172, 320])
    run('pseudo', [args.pseudo, html, css, pseudo, 172, 320, '--diagnostics-json', diagnostics])
    reference = pixels(output)
    require(reference == pixels(explicit) == pixels(pseudo), 'Native Core default/explicit/pseudo pixels differ')
    require(len(set(reference[2])) > 2, 'Expected grayscale coverage from Core font')
    require(json.loads(diagnostics.read_text())['textPolicy'] == 'core-text', 'Incorrect text-policy diagnostics')

    app = root / 'app'
    app.mkdir()
    manifest = dict(id='org.jellyframe.native-core-probe', name='Native Core Probe', entry='/index.html',
                    runtime=dict(minJellyFrame='0.6.0', minRenderCore=args.core_version, script='classic'),
                    viewport=dict(designWidth=172, designHeight=320))
    manifest_path = app / 'jellyframe.app.json'
    manifest_path.write_text(json.dumps(manifest))
    # DOM mutation forces a second layout and paint, exercising more than startup.
    (app / 'index.html').write_text(
        '<style>body{margin:0;background:#fff;color:#000}div{font-size:24px}</style>'
        '<div id="label">ABC office 123</div><div id="next">NEXT</div>'
        '<script>document.getElementById("next").addEventListener("click",function(){'
        'document.getElementById("label").textContent="XYZ 456";});</script>', encoding='utf-8')
    frames = root / 'frames'
    trace = root / 'trace.jsonl'
    run('mutation', [args.desktop, '--app', app, '--capture-frames', frames, '--frame-count', 4,
                     '--frame-event', '1:pointer-down:5:48', '--frame-event', '2:pointer-up:5:48',
                     '--render-trace', trace])
    # Capture frames use BMP regardless of single-capture output extension.
    def frame_rgb(path):
        data = path.read_bytes()
        require(data[:2] == b'BM', 'Expected BMP frame')
        offset = struct.unpack_from('<I', data, 10)[0]
        width, height = struct.unpack_from('<ii', data, 18)
        require((width, height) == (172, 320) and struct.unpack_from('<H', data, 28)[0] == 24,
                'Expected 172x320 RGB24 capture')
        stride = (width * 3 + 3) & ~3
        return bytes(data[offset + (height - 1 - y) * stride + x * 3 + channel]
                     for y in range(height) for x in range(width) for channel in (2, 1, 0))
    first = frame_rgb(frames/'frame_000.bmp')
    changed = frame_rgb(frames/'frame_002.bmp')
    stable = frame_rgb(frames/'frame_003.bmp')
    require(first != changed and changed == stable, 'Click mutation missing or not stable')
    html.write_text('<!doctype html><body><div>XYZ 456</div><div>NEXT</div></body>', encoding='utf-8')
    css.write_text('body{margin:0;background:#fff;color:#000}div{font-size:24px}', encoding='utf-8')
    run('mutation-reference', [args.pseudo, html, css, root/'changed.ppm', 172, 320])
    require(changed == pixels(root/'changed.ppm')[2], 'Post-mutation frame differs from fresh Core render')

    # Interaction style changes need fresh glyph metrics too; they must not
    # mutate an opaque Core snapshot through the legacy style-stable shortcut.
    (app / 'index.html').write_text(
        '<style>body{margin:0;background:#fff;color:#000}'
        '#label{font-size:24px}#label:hover{font-size:32px;background:#ddeeff}</style>'
        '<div id="label">ABC 123</div>', encoding='utf-8')
    hover_frames = root / 'hover-frames'
    run('hover', [args.desktop, '--app', app, '--capture-frames', hover_frames, '--frame-count', 3,
                  '--frame-event', '1:pointer-move:5:5', '--render-trace', root/'hover-trace.jsonl'])
    hovered = frame_rgb(hover_frames/'frame_001.bmp')
    require(frame_rgb(hover_frames/'frame_000.bmp') != hovered and
            hovered == frame_rgb(hover_frames/'frame_002.bmp'), 'Hover font update missing or unstable')
    html.write_text('<body><div id="label">ABC 123</div></body>', encoding='utf-8')
    css.write_text('body{margin:0;background:#fff;color:#000}'
                   '#label{font-size:32px;background:#ddeeff}', encoding='utf-8')
    run('hover-reference', [args.pseudo, html, css, root/'hovered.ppm', 172, 320])
    require(hovered == pixels(root/'hovered.ppm')[2], 'Hover frame differs from fresh Core render')

    manifest['runtime']['minRenderCore'] = '0.1.0' if args.core_version != '0.1.0' else '0.2.0'
    manifest_path.write_text(json.dumps(manifest))
    refused = run('version-refusal', [args.desktop, '--app', app, '--capture', root/'bad-version.ppm'], False)
    require('configured pre-1.0 Render Core line' in refused, 'Version policy not enforced')
    manifest['runtime']['minRenderCore'] = args.core_version
    manifest['textPipeline'] = None
    manifest_path.write_text(json.dumps(manifest))
    refused = run('contextual-refusal', [args.desktop, '--app', app, '--capture', root/'bad-route.ppm'], False)
    require('does not support package-selected shaped textPipeline' in refused, 'Scalar native route accepted shaped declaration')
    require(not (root/'bad-version.ppm').exists() and not (root/'bad-route.ppm').exists(), 'Refused package produced output')

    if args.core_abi >= 9:
        # Out-of-repertoire scalar input must refuse, never null-dereference or
        # silently fall back to GDI. This does not qualify contextual shaping.
        html.write_text('<body><div>\u0627\u0644\u0639\u0631\u0628\u064a\u0629</div></body>', encoding='utf-8')
        refused = run('unsupported-layout', [args.desktop, '--capture', root/'unsupported.ppm', html, css, 172, 320], False)
        require('layout refused (Core text status=' in refused, 'Missing controlled layout refusal')
        require(not (root/'unsupported.ppm').exists(), 'Unsupported layout produced a success image')
    (root/'result.json').write_text(json.dumps(dict(defaultCoreText=True, exactPixels=True,
        mutationAndFreshRenderMatch=True, hoverAndFreshRenderMatch=True,
        strictVersionRefusal=True, shapedRouteRefused=True,
        abi=args.core_abi, contextualDesktopQualified=False), indent=2)+'\n')
    print('Native Core text default, exact pixels, JS mutation, strict version and route refusal passed')


if args.output:
    args.output.mkdir(parents=True, exist_ok=False)
    root = args.output
    check()
else:
    with tempfile.TemporaryDirectory(prefix='native-core-route-') as directory:
        root = Path(directory)
        check()
