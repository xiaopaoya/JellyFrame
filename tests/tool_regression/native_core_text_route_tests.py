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

    # Real package decoding must precede the immutable Core image proof, and
    # fractional image commands need a painter; text-only pixels are not success.
    image_results = {}
    if args.core_abi >= 9:
        html.write_text('<body>' + '<i></i>' * 600 + '</body>', encoding='utf-8')
        css.write_text('body{margin:0}i{display:block;width:1px;height:1px;background:#000}', encoding='utf-8')
        dense_diagnostics = root/'dense.json'
        run('dense', [args.pseudo, html, css, root/'dense.ppm', 100, 100,
                      '--diagnostics-json', dense_diagnostics])
        dense = json.loads(dense_diagnostics.read_text())
        require(dense['pipeline']['displayCommands'] >= 600,
                'Dense admission must not silently truncate the original 600 nodes')
        codes = {item['code'] for item in dense['diagnostics']}
        require({'visual-display-command-density', 'visual-scroll-needed'} <= codes,
                'Dense admission must retain density and overflow diagnostics')
        width, height = 12, 8
        stride = (width * 3 + 3) & ~3
        bitmap = (bytes((0, 220, 50)) * width + bytes(stride - width * 3)) * height
        header = struct.pack('<2sIHHI', b'BM', 54 + len(bitmap), 0, 0, 54)
        header += struct.pack('<IiiHHIIiiII', 40, width, height, 1, 24, 0, len(bitmap), 0, 0, 0, 0)
        (app/'tile.bmp').write_bytes(header + bitmap)
        for mode, image_css, min_pixels, max_pixels in (
                # The explicit-size reference uses the same shared-inline
                # baseline path as the default inline-block auto-size image.
                ('explicit', 'img{display:inline;width:24px;height:16px}', 384, 400),
                ('auto', 'img{width:24px;height:auto}', 384, 400),
                ('natural', '', 96, 104)):
            (app/'index.html').write_text(
                '<style>body{margin:0;background:#fff;color:#000}div{font-size:16px}' + image_css +
                '</style><div>A<img src="tile.bmp">B</div>', encoding='utf-8')
            image_log = run('image-'+mode, [args.desktop, '--app', app, '--capture',
                root/('image-'+mode+'.ppm')])
            _, _, rgb = pixels(root/('image-'+mode+'.ppm'))
            green = sum(rgb[i:i+3] == bytes((50, 220, 0)) for i in range(0, len(rgb), 3))
            require(min_pixels <= green <= max_pixels,
                    f'{mode} image absent or incorrectly sized: {green} pixels '
                    f'(expected {min_pixels}..{max_pixels}); '
                    f'log={image_log}')
            image_results[mode] = green
        require(pixels(root/'image-explicit.ppm') == pixels(root/'image-auto.ppm'),
                'Natural aspect ratio differs from equivalent explicit image geometry')
        (app/'index.html').write_text(
            '<style>body{margin:0;background:#fff}div{width:10px;height:40px;overflow:hidden}'
            'img{width:24px;height:16px}</style><div><img src="tile.bmp"></div>', encoding='utf-8')
        run('image-clip', [args.desktop, '--app', app, '--capture', root/'image-clip.ppm'])
        _, _, clipped_rgb = pixels(root/'image-clip.ppm')
        require(sum(clipped_rgb[i:i+3] == bytes((50, 220, 0))
                    for i in range(0, len(clipped_rgb), 3)) == 160,
                'Fractional image painter did not respect the parent clip')
        (app/'index.html').write_text(
            '<style>body{margin:0;background:#fff;color:#000}div{font-size:16px}</style>'
            '<div>A<img src="tile.bmp">B</div>', encoding='utf-8')
        # Pointer capture exercises the separate interactive pipeline, not only
        # the single-capture implementation.
        image_frames = root/'image-frames'
        run('image-frames', [args.desktop, '--app', app, '--capture-frames', image_frames,
                            '--frame-count', 3, '--render-trace', root/'image-trace.jsonl'])
        require(frame_rgb(image_frames/'frame_000.bmp') == pixels(root/'image-natural.ppm')[2] ==
                frame_rgb(image_frames/'frame_002.bmp'), 'Interactive image capture is missing or unstable')
        red_width, red_height = 6, 12
        red_stride = (red_width * 3 + 3) & ~3
        red_bitmap = (bytes((20, 30, 230)) * red_width + bytes(red_stride - red_width * 3)) * red_height
        red_header = struct.pack('<2sIHHI', b'BM', 54 + len(red_bitmap), 0, 0, 54)
        red_header += struct.pack('<IiiHHIIiiII', 40, red_width, red_height, 1, 24, 0,
                                  len(red_bitmap), 0, 0, 0, 0)
        (app/'other.bmp').write_bytes(red_header + red_bitmap)
        (app/'index.html').write_text(
            '<style>body{margin:0;background:#fff;color:#000}div{font-size:24px}'
            'img{width:auto;height:16px}</style>'
            '<div>A<img id="picture" src="tile.bmp">B<img src="other.bmp"></div>'
            '<div id="next">NEXT</div><script>document.getElementById("next").addEventListener("click",'
            'function(){document.getElementById("picture").setAttribute("src","other.bmp");});</script>',
            encoding='utf-8')
        replacement_frames = root/'replacement-frames'
        run('image-replacement', [args.desktop, '--app', app, '--capture-frames', replacement_frames,
            '--frame-count', 4, '--frame-event', '1:pointer-down:5:48', '--frame-event', '2:pointer-up:5:48',
            '--render-trace', root/'replacement-trace.jsonl'])
        initial = frame_rgb(replacement_frames/'frame_000.bmp')
        replaced = frame_rgb(replacement_frames/'frame_002.bmp')
        require(initial != replaced and replaced == frame_rgb(replacement_frames/'frame_003.bmp'),
                'Image src replacement did not update or remain stable')
        initial_green = sum(initial[i:i+3] == bytes((50, 220, 0)) for i in range(0, len(initial), 3))
        require(384 <= initial_green <= 400,
                'Initial image missing before replacement')
        replaced_red = sum(replaced[i:i+3] == bytes((230, 30, 20)) for i in range(0, len(replaced), 3))
        require(256 <= replaced_red <= 288,
                'Ready-to-ready image replacement used stale source or geometry')
        image_results['readyReplacement'] = True
        (app/'index.html').write_text('<style>body{margin:0}img{width:24px;height:16px}</style>'
                                    '<div>A<img src="missing.bmp">B</div>', encoding='utf-8')
        refused = run('missing-image', [args.desktop, '--app', app, '--capture', root/'missing-image.ppm'], False)
        require('required content image failed' in refused and not (root/'missing-image.ppm').exists(),
                'Missing required image must not publish a successful partial frame')
        (app/'index.html').write_text(
            '<style>body{margin:0}img{display:none}</style><div>VISIBLE</div><img src="missing.bmp">',
            encoding='utf-8')
        run('hidden-missing-image', [args.desktop, '--app', app, '--capture', root/'hidden-missing-image.ppm'])
        require((root/'hidden-missing-image.ppm').exists(),
                'A failed image omitted from the layer tree must not refuse the frame')

    manifest['runtime']['minRenderCore'] = '0.1.0' if args.core_version != '0.1.0' else '0.2.0'
    manifest_path.write_text(json.dumps(manifest))
    refused = run('version-refusal', [args.desktop, '--app', app, '--capture', root/'bad-version.ppm'], False)
    require('configured pre-1.0 Render Core line' in refused, 'Version policy not enforced')
    manifest['runtime']['minRenderCore'] = args.core_version
    manifest['textPipeline'] = None
    manifest_path.write_text(json.dumps(manifest))
    refused = run('contextual-refusal', [args.desktop, '--app', app, '--capture', root/'bad-route.ppm'], False)
    require(('requires an explicit jfsp2-block-v12 textPipeline' in refused or
             'does not support package-selected shaped textPipeline' in refused),
            'Null textPipeline did not produce a controlled route refusal')
    require(not (root/'bad-version.ppm').exists() and not (root/'bad-route.ppm').exists(), 'Refused package produced output')

    if args.core_abi >= 9:
        # Out-of-repertoire scalar input must refuse, never null-dereference or
        # silently fall back to GDI. This does not qualify contextual shaping.
        html.write_text('<body><div>\u0627\u0644\u0639\u0631\u0628\u064a\u0629</div></body>', encoding='utf-8')
        refused = run('unsupported-layout', [args.desktop, '--capture', root/'unsupported.ppm', html, css, 172, 320], False)
        require('Core text layout refused' in refused, 'Missing controlled layout refusal')
        require(not (root/'unsupported.ppm').exists(), 'Unsupported layout produced a success image')
    (root/'result.json').write_text(json.dumps(dict(defaultCoreText=True, exactPixels=True,
        mutationAndFreshRenderMatch=True, hoverAndFreshRenderMatch=True,
        strictVersionRefusal=True, shapedRouteRefused=True,
        packageImagePixels=image_results,
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
