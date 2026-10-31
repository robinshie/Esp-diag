#!/usr/bin/env python3
"""Check split isolation and Python/C++ prediction parity, including bounds."""
import json
import subprocess
import tempfile
from pathlib import Path
from train import ROOT, features

sets = json.loads((ROOT / 'artifacts/split.json').read_text())
groups = [{r['template_group'] for r in rows} for rows in sets.values()]
assert not any(a & b for i, a in enumerate(groups) for b in groups[i + 1:])
signatures=[{tuple(features(r['text'])) for r in rows} for rows in sets.values()]
assert not any(a & b for i,a in enumerate(signatures) for b in signatures[i+1:]), 'feature-identical logs crossed splits'
sources={s['id'] for s in json.loads((ROOT/'corpus/sources.json').read_text())['sources']}
for rows in sets.values():
    for row in rows:
        if row.get('source_id'):assert row['source_id'] in sources
fixtures = json.loads((ROOT / 'artifacts/parity.json').read_text())
with tempfile.TemporaryDirectory(prefix='tiny-log-') as temp:
    binary = str(Path(temp) / 'infer')
    subprocess.run(['c++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    str(ROOT / 'native.cpp'), '-o', binary], check=True)
    result = subprocess.run([binary], input='\n'.join(f['text'] for f in fixtures) + '\n',
                            text=True, capture_output=True, check=True)
    lines = result.stdout.splitlines()
    assert len(lines) == len(fixtures)
    for fixture, line in zip(fixtures, lines):
        label, accepted, truncated, *scores = line.split()
        assert max(abs(float(a) - b) for a, b in zip(scores, fixture['scores'])) < 0.00001, fixture['text']
        if len(fixture['text']) > 1024 or not fixture['text'].strip(' ?'):
            assert accepted == '0'
    print(f'{len(fixtures)} C++/Python parity cases passed; ASan/UBSan passed; groups and feature signatures isolated; sources verified.')
