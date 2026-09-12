#!/usr/bin/env python3
"""Replay approved showcase interactions in the scripting-enabled Win32 shell.

Usage: app_showcase_capture_tests.py BUILD_TOOL_DIR OUTPUT_DIR
Requires the native scripting shell and CLI tools; uses no browser or Pillow.
"""
from pathlib import Path
import json
import subprocess
import sys
from package_image_fixture_tests import read_bmp_pixels

ROOT = Path(__file__).resolve().parents[2]
VIEWS = [('round-300', 300, 300), ('rect-320x240', 320, 240), ('rect-172x320', 172, 320)]

def run(args, log, cwd):
    log.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([str(x) for x in args], cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=120)
    log.write_bytes(result.stdout)
    output = result.stdout.decode('utf-8', errors='replace')
    if result.returncode or 'script failed' in output or '[error]' in output or 'REVIEW_ASSERT' in output:
        raise AssertionError(f'Native command failed; see {log}\n{output[-4000:]}')

scenarios={
'templates/calculator':{
 'frames':24,'events':[(1,'click-id key-7'),(2,'click-id key-plus'),(3,'click-id key-8'),(4,'click-id key-equals'),(7,'click-id key-equals'),(10,'click-id key-minus'),(11,'click-id key-3'),(12,'click-id key-plus'),(13,'click-id key-2'),(14,'click-id key-equals'),(18,'click-id key-clear'),(20,'click-id key-equals')],
 'checks':[(600,'eq("display","15",true);'),(900,'eq("display","15",true);'),(1600,'eq("display","14",true);'),(2200,'eq("display","0",true);')], 'show':[0,5,15,22]},
'templates/clock':{
 'frames':18,'events':[(3,'click-id zoneButton'),(8,'click-id formatButton'),(13,'click-id zoneButton')],
 'checks':[(200,'eq("time","22:13");'),(600,'eq("time","06:13"); eq("phase","Morning");'),(1100,'eq("formatButton","12 hour");'),(1600,'eq("time","10:13");')], 'show':[0,6,11,16]},
'templates/timer':{
 'frames':32,'events':[(1,'click-id toggle'),(8,'click-id toggle'),(14,'click-id toggle'),(19,'time-ms 1700000065000'),(24,'click-id toggle'),(28,'click-id reset')],
 'checks':[(500,'eq("state","Focusing");'),(1100,'eq("state","Paused");'),(1700,'eq("state","Focusing");')], 'show':[0,6,11,21,26,30]},
'templates/weather':{
 'frames':18,'events':[(3,'click-id hourly'),(8,'click-id air'),(13,'click-id daily')],
 'checks':[(600,'eq("condition","Rain soon"); eq("temp","27");'),(1100,'eq("unit","AQI"); eq("temp","42");'),(1600,'eq("condition","Cloudy"); eq("unit","C");')], 'show':[0,6,11,16]},
'packages/watch_weather':{
 'frames':18,'events':[(3,'click-id hourly'),(8,'click-id air'),(13,'click-id daily')],
 'checks':[(600,'eq("condition","Rain soon");'),(1100,'eq("unit","AQI");'),(1600,'eq("unit","C");')], 'show':[0,6,11,16]},
'packages/jelly_controls':{
 'frames':22,'events':[(2,'set-value goal Night%20reading'),(4,'set-value brightness 72'),(7,'set-checked quiet 0'),(11,'click-id save'),(15,'set-checked quiet 1'),(17,'click-id save')],
 'checks':[(600,'eq("pct","72%"); eq("goal","Night reading",true);'),(1400,'eq("toast","Saved / Quiet mode off");'),(2000,'eq("toast","Saved / Quiet mode on");')], 'show':[0,6,14,20]},
'packages/jelly_route_tabs':{
 'frames':24,'events':[(3,'click-id focus'),(8,'click-id settings'),(13,'click-id back'),(18,'click-id today')],
 'checks':[(600,'eq("title","Focus"); eq("value","25 min");'),(1100,'eq("title","Settings");'),(1600,'eq("title","Focus");'),(2100,'eq("title","Today");')], 'show':[0,6,11,16,21]},
'packages/jelly_motion_lab':{
 'frames':36,'events':[(8,'click-id slide'),(20,'click-id play'),(30,'click-id play')],
 'checks':[(1100,'eq("motion-label","Slide");'),(2300,'eq("play","Play");'),(3300,'eq("play","Pause");')], 'show':[0,7,16,24,34]},
'packages/jelly_static_modules':{
 'frames':14,'events':[(3,'click-id add'),(8,'click-id add')],
 'checks':[(600,'eq("value","30m");'),(1100,'eq("value","35m");')], 'show':[0,6,11]},
}
def probe_script(checks,path):
    code='''(function () {
var start = Date.now(), count = 0, failed = false;
var marker = document.createElement("div");
marker.style.position="absolute"; marker.style.left="0px"; marker.style.top="0px";
marker.style.width="2px"; marker.style.height="2px"; marker.style.background="#ff0000";
document.body.appendChild(marker);
function eq(id, expected, value) {
 var el=document.getElementById(id), actual=value ? el.value : el.textContent;
 if(actual!==expected) { failed=true; throw new Error("REVIEW_ASSERT " + id + " expected=" + expected + " actual=" + actual); }
}
var checks = [
'''+',\n'.join('{at:'+str(at)+',run:function(){'+test+'}}' for at,test in checks)+'''];
var timer=setInterval(function(){
 if(count < checks.length && Date.now()-start >= checks[count].at){
  checks[count].run(); count+=1;
 }
 if(count==checks.length){ if(!failed){ marker.style.background="#00ff00"; } clearInterval(timer); }
},100);
}());'''
    path.write_text(code,encoding='utf-8')

def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    build = Path(sys.argv[1]).resolve()
    out = Path(sys.argv[2]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    shell = build / 'jellyframe_desktop_shell.exe'
    results = []
    for key, spec in scenarios.items():
        group, name = key.split('/')
        app = ROOT / ('tools/templates/apps' if group == 'templates' else 'samples/apps/packages') / name
        dest = out / key
        dest.mkdir(parents=True, exist_ok=True)
        source = app
        if name == 'jelly_static_modules':
            source = dest / 'app.jfapp'
            run([sys.executable, ROOT/'tools/jellyframe_cli.py', 'package', '--root', app,
                 '--output-bundle', source, '--report', dest/'package.json',
                 '--build-dir', build], dest/'package.log', out)
        for target, width, height in VIEWS:
            folder = dest / target
            folder.mkdir(parents=True, exist_ok=True)
            script = folder / 'interaction.jfcapture'
            script.write_text('\n'.join([f'frames {spec["frames"]}', 'step-ms 100',
                'start-ms 1700000000000', f'viewport {width} {height}'] +
                [f'event {frame} {event}' for frame, event in spec['events']])+'\n', encoding='utf-8')
            args = [shell, '--app', source, '--frame-script', script]
            for mode in ['frames', 'full']:
                cmd = args + ['--capture-frames', folder/mode]
                if mode == 'full': cmd += ['--force-full-repaint']
                run(cmd, folder/(mode+'.log'), out)
            frame_names = [f'frame_{i:03}.bmp' for i in range(spec['frames'])]
            for frame in frame_names:
                normal = read_bmp_pixels(folder/'frames'/frame)
                full = read_bmp_pixels(folder/'full'/frame)
                if normal != full: raise AssertionError(f'Repaint mismatch: {key}/{target}/{frame}')
            if name == 'jelly_motion_lab':
                paused = [read_bmp_pixels(folder/'frames'/f'frame_{i:03}.bmp') for i in range(23,29)]
                if any(frame != paused[0] for frame in paused[1:]):
                    raise AssertionError(f'Paused motion changed: {target}')
            if target == 'round-300':
                probe = folder/'assertions.js'
                probe_script(spec['checks'], probe)
                run(args + ['--capture-frames', folder/'assert', '--script', probe], folder/'assert.log', out)
                last = read_bmp_pixels(folder/'assert'/frame_names[-1])
                if last[2][0] != (0,255,0): raise AssertionError(f'DOM assertions incomplete: {key}')
            results.append(dict(app=key, target=target, comparedFrames=spec['frames'],
                                domAssertions=target=='round-300'))
            print(f'{key} {target}: passed', flush=True)
    (out/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(f'{len(results)} groups, {sum(r["comparedFrames"] for r in results)} frame pairs passed')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
