"""Send the complete executable fixture through the actual provider, mocked wire only."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tests/tool_regression'))
from signed_provider_tests import PhysicalTransportTests, provider, parse_provider_jsonl


def main():
    fixture = Path(sys.argv[1])
    raw = (fixture / 'full-shaped.jfsg').read_bytes()
    evidence = json.loads((fixture / 'package-evidence.json').read_text(encoding='utf-8'))
    assert hashlib.sha256(raw).hexdigest() == evidence['signedSha256']
    assert evidence['fontGlyphs'] >= 4000 and evidence['omittedOriginalAssets'] == []
    pipeline = evidence.get('textPipeline', 'jfsp1-block-v11')
    assert pipeline in ('jfsp1-block-v11', 'jfsp2-block-v12')
    contextual = pipeline == 'jfsp2-block-v12'
    abi = 9 if contextual else 4
    features = ['text.contextual-jfsp2', 'text.contextual-layout'] if contextual else ['text.shaping']
    case = PhysicalTransportTests(methodName='test_large_signed_exact_bytes_once_read_and_bounded_jsonl')
    with tempfile.TemporaryDirectory(prefix='full-shaped-wire-') as temp:
        root = Path(temp)
        (code, events), received, calls = case.exercise(raw, root, cap=2093056, replace=True,
                                                       abi=abi, features=features)
        case.assertEqual((code, received), (0, raw))
        case.assertEqual(calls[-1], provider.INSTALL_COMMIT)
        text = '\n'.join(json.dumps(event) for event in events)
        case.assertLess(len(text.encode()), 256 * 1024)
        case.assertEqual(parse_provider_jsonl(text)[-1]['resultCode'], 'ok')
        refused = [(1044480, abi, features), (2093056, abi, [])]
        refused += ([(2093056, 4, ['text.shaping']),
                     (2093056, 9, ['text.contextual-jfsp2']),
                     (2093056, 9, ['text.contextual-layout']),
                     (2093056, 4, features)] if contextual else
                    [(2093056, 9, ['text.contextual-jfsp2', 'text.contextual-layout'])])
        for cap, wrong_abi, wrong_features in refused:
            with case.assertRaises(provider.ProviderError):
                case.exercise(raw, root, cap=cap, abi=wrong_abi, features=wrong_features)
    print(json.dumps({'bytes':len(raw),'sha256':evidence['signedSha256'],
                      'wireMocked':True,'events':len(events),'textPipeline':pipeline,
                      'abi':abi,'preBeginRefusals':len(refused),'result':'PASS'}))


if __name__ == '__main__':
    main()
