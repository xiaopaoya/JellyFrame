"""Build a complete, locally signed shaping workload using original assets.

Only --public-test-signature enables the deliberately public P-256 scalar1 key.
This neither reads private keys nor changes any target's trust configuration.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import package_app
import sign_app_package
from device_install_package import inspect_install_bytes


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build(core_fixtures, font, output, capacity, public_test_signature=False):
    core_fixtures, font, output = Path(core_fixtures), Path(font), Path(output)
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError('output already exists; preserve previous package evidence')
    if type(capacity) is not int or not 0 < capacity <= 4 * 1024 * 1024:
        raise ValueError('invalid explicit package capacity')
    records = json.loads((core_fixtures / 'shaping_executor_catalog/manifest.json').read_text(encoding='utf-8'))['faces']['Regular']
    companion_record = json.loads((core_fixtures / 'shaping_resource/manifest.json').read_text(encoding='utf-8'))['faces']['Regular']
    sources = {
        '/font.jffont2': font,
        '/catalog.jfcat1': core_fixtures / 'shaping_executor_catalog/regular.jfcat1',
        '/companion.jfshr2': core_fixtures / 'shaping_resource/regular.jfshr2',
        '/unicode14.bin': core_fixtures / 'shaping_normalization/unicode14.bin',
        '/regular.eligibility': core_fixtures / 'shaping_normalization/regular.eligibility',
        '/LICENSE-NOTO-SANS-SC.txt': ROOT / 'ports/esp32s3-idf/font/LICENSE-NOTO-SANS-SC.txt',
    }
    assets = {name: path.read_bytes() for name, path in sources.items()}
    raw_font, catalog, companion = (assets[name] for name in ('/font.jffont2','/catalog.jfcat1','/companion.jfshr2'))
    if (len(raw_font) < 128 or raw_font[:8] != b'JFFONT2\0' or
            raw_font[64:96].hex() != records['sourceSha256'] or
            struct.unpack_from('<HH', raw_font, 38) != (16, 400) or
            struct.unpack_from('<I', raw_font, 20)[0] < 4000):
        raise ValueError('requires complete matching regular16 JFFONT2, not a reduced glyph fixture')
    if (sha(catalog) != records['sha256'] or sha(companion) != records['companionSha256'] or
            sha(companion) != companion_record['fixtureSha256'] or
            catalog[16:48] != raw_font[64:96] or catalog[48:80].hex() != records['profileSha256']):
        raise ValueError('catalog/companion/font evidence mismatch')
    html = ('<html><head><link rel="stylesheet" href="/app.css"></head><body>'
            '<div id="title">SHAPED CORE</div><div id="label">office AV fi 中 e\u0301</div>'
            '<div id="button">NEXT 0</div><script src="/app.js"></script></body></html>')
    css = ('*{margin:0;padding:0;font-weight:400}body{margin:0;padding:0}'
           'div{display:block;font-size:16px;line-height:22px;color:#223344}'
           '#title{font-size:20px;line-height:26px;margin-bottom:6px}'
           '#label{width:148px;margin-bottom:8px;background-color:#fff0aa}'
           '#button{width:144px;line-height:28px;color:#ffffff;background-color:#2255bb}')
    script = ("var count=0;document.getElementById('button').addEventListener('click',function(){"
              "count++;document.getElementById('label').textContent=count%2?'AV office fi 中文':'office AV fi 中 e\\u0301';"
              "document.getElementById('label').style.width=count%2?'104px':'148px';"
              "document.getElementById('button').textContent='NEXT '+count;});")
    assets.update({'/index.html': html.encode('utf-8'), '/app.css': css.encode(), '/app.js': script.encode()})
    manifest = package_app.read_manifest(ROOT / 'samples/apps/packages/jelly_font_policy')
    manifest['id'] = 'org.jellyframe.signed-test'
    manifest['name'] = 'Signed Full Shaping'
    manifest['version'] = {'name': '1.0.1', 'code': 1}
    manifest['fonts'] = []
    manifest['targets'] = {'ws147':{'viewport':{'width':172,'height':320,'shape':'rect'},'output':'jfapp'}}
    manifest['runtime']['script'] = 'classic'
    manifest['viewport'] = {'designWidth': 172, 'designHeight': 320, 'shape': 'rect'}
    manifest['budgets'] = {'maxResourceBytes': 2 * 1024 * 1024, 'maxDomNodes':128,
                           'maxCssRules':64,'maxDisplayCommands':256,'maxTimers':8,'maxEventListeners':16}
    manifest['textPipeline'] = {
        'kind':'jfsp1-block-v11','font':'/font.jffont2','catalog':'/catalog.jfcat1',
        'companion':'/companion.jfshr2','tables':'/unicode14.bin','eligibility':'/regular.eligibility',
        'sourceIdentity':records['sourceSha256'],'profileIdentity':records['profileSha256'],
        'glyphCount':companion_record['glyphCount'],'resourceId':55001,'rasterSize':16,'weight':400,'selection':0,
    }
    manifest = package_app.validate_manifest(manifest)
    output.mkdir(parents=True,exist_ok=True)
    for name, data in assets.items():
        (output / name[1:]).write_bytes(data)
    (output / 'jellyframe.app.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2),encoding='utf-8')
    resources = [{'path':name,'file':output/name[1:],'kind':package_app.resource_kind(Path(name))} for name in assets]
    bundle_path = output / 'full-shaped.jfapp'
    package_app.write_jfapp_bundle(bundle_path, manifest, resources)
    package = bundle_path.read_bytes()
    signed = None
    if public_test_signature:
        from cryptography.hazmat.primitives.asymmetric import ec
        signed = sign_app_package.sign_bundle(package, ec.derive_private_key(1,ec.SECP256R1()),bytes(range(0x10,0x20)))
        (output / 'full-shaped.jfsg').write_bytes(signed)
    report = {'assets':[{'path':name,'bytes':len(data),'sha256':sha(data)} for name,data in sorted(assets.items())],
              'assetBytes':sum(map(len,assets.values())), 'bundleBytes':len(package),'bundleSha256':sha(package),
              'signedBytes':len(signed) if signed else None,'signedSha256':sha(signed) if signed else None,
              'maxPackageBytes':capacity, 'fits':len(signed or package) <= capacity,
              'publicTestKey':public_test_signature,'sourceIdentity':records['sourceSha256'],
              'profileIdentity':records['profileSha256'],'fontGlyphs':struct.unpack_from('<I',raw_font,20)[0],
              'catalogRasterGids':records['rasterGids'], 'omittedOriginalAssets':[],
              'executionVerified':False}
    if signed and report['fits']:
        report['hostInspection'] = inspect_install_bytes(signed, capacity, supported_text_pipelines=('jfsp1-block-v11',))
    (output / 'package-evidence.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    if not report['fits']:
        raise ValueError(f"full resource package exceeds target capacity: {len(signed or package)} > {capacity}; report retained")
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core-fixtures',type=Path,required=True)
    parser.add_argument('--font',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--max-package-bytes',type=int,required=True)
    parser.add_argument('--public-test-signature',action='store_true')
    args=parser.parse_args()
    try:
        report=build(args.core_fixtures,args.font,args.output,args.max_package_bytes,args.public_test_signature)
    except (ValueError,OSError,KeyError,struct.error) as error:
        parser.exit(1,f'prepare_signed_shaped_fixture: {error}\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
