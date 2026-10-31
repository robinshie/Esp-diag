#!/usr/bin/env python3
"""Reproducible softmax training on authored official-derived + synthetic logs."""
import hashlib
import json
import math
import random
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DIM = 2048
SEED = 20261031
VERSION = 'tiny-log-v2-h3c'
SKIP_VALUE = {'device', 'component', 'incident', 'request', 'req', 'boot', 'boot_id',
              'uptime_ms', 'unix_ms', 'duration_ms', 'first_uptime_ms', 'nonce'}


def fnv(text, dimension=DIM):
    value = 2166136261
    for byte in text.encode('ascii'):
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value % dimension


def features(text, dimension=DIM):
    tokens = re.findall(r'[a-z0-9_]+', text[:1024].lower())
    words, skip = [], False
    for word in tokens:
        if skip:
            skip = False
            continue
        if word in SKIP_VALUE:
            skip = True
            continue
        if len(word) > 32 or any('0' <= char <= '9' for char in word):
            continue
        words.append(word)
    indices = {fnv('w:' + word, dimension) for word in words}
    # Log enum words (ETH_LINK_DOWN) share evidence with plain words (link down).
    for word in words:
        if '_' in word:
            parts = [part for part in word.split('_') if part]
            indices.update(fnv('w:' + part, dimension) for part in parts)
            indices.update(fnv('b:' + a + ':' + b, dimension) for a, b in zip(parts, parts[1:]))
    indices.update(fnv('b:' + a + ':' + b, dimension) for a, b in zip(words, words[1:]))
    return sorted(indices)


def probabilities(weights, bias, indices):
    scale = 1 / math.sqrt(max(1, len(indices)))
    scores = [b + sum(row[i] for i in indices) * scale for b, row in zip(bias, weights)]
    maximum = max(scores)
    values = [math.exp(s - maximum) for s in scores]
    total = sum(values)
    return [v / total for v in values]


def dataset():
    templates = json.loads((ROOT / 'samples.json').read_text())
    templates.update(json.loads((ROOT / 'corpus/additional.json').read_text()))
    labels = list(templates)
    rng = random.Random(SEED)
    sets = {key: [] for key in ('train', 'validation', 'test')}
    # Whole templates stay in ONE split. IDs and field variations never cross it.
    for label, phrases in templates.items():
        order = list(range(len(phrases)))
        rng.shuffle(order)
        for position, group in enumerate(order):
            split = 'train' if position < 6 else 'validation' if position < 8 else 'test'
            for variation in range(10):
                text = phrases[group].format(
                    device=f'sw-{rng.randrange(100, 999)}', port=f'GE1/0/{rng.randrange(1, 49)}',
                    id=f'inc-{rng.randrange(10000, 99999)}', time=rng.randrange(100, 999999),
                    action=rng.choice(['UNLOCK', 'RECOVER_INTERFACE']),
                    fault=rng.choice(['AUTH_LOCKED', 'LINK_DOWN', 'THERMAL', 'FAN', 'LOOP', 'POWER']))
                if variation % 3 == 0:
                    text = text.upper()
                sets[split].append({'label': label, 'template_group': f'{label}:{group}', 'text': text})
    # Official message identifiers and authored paraphrases are NOT real logs.
    official = json.loads((ROOT / 'corpus/h3c.json').read_text())
    families = sorted({row['family'] for row in official})
    rng.shuffle(families)
    # Keep the entire mnemonic family (including clear/up variants) in one split.
    # Greedily reserve enough train families to cover each official-derived intent.
    training_families, covered = set(), set()
    for family in families:
        intents = {row['label'] for row in official if row['family'] == family}
        if intents - covered:
            training_families.add(family)
            covered.update(intents)
    remainder = [family for family in families if family not in training_families]
    validation = set(remainder[:max(1, len(families) // 6)])
    testing = set(remainder[max(1, len(families) // 6):max(1, len(families) // 6) * 2])
    def values():
        return dict(device=f'LAB-SW-{rng.randrange(100,999)}',
                    port=f'GigabitEthernet1/0/{rng.randrange(1,49)}',
                    peer=f'GigabitEthernet2/0/{rng.randrange(1,49)}',
                    vlan=rng.randrange(1,4095), slot=rng.randrange(1,9))
    for row in official:
        split = 'validation' if row['family'] in validation else 'test' if row['family'] in testing else 'train'
        for variation in range(12):
            variables = values()
            body = row['body'].format(**variables)
            # Severity is deliberately NOT a class shortcut: up/down can share it.
            severity = rng.randrange(1,7)
            header = f'{row["module"]}/{severity}/{row["mnemonic"]}: '
            prefix = ['', '%Oct 31 12:34:56:000 2026 {device} ',
                      '<188>Oct 31 12:34:56 {device} %%10'][variation % 3].format(**variables)
            sets[split].append({'label':row['label'],'template_group':row['family'],
                               'source_id':row['source_id'],'kind':row['kind'],
                               'text':prefix+header+body})
    # Train-only adapters for the existing ESP normalized protocol. All renderings
    # of the same event family stay together and are excluded from holdout metrics.
    events = {'AUTH_LOCKED':'auth_lock','LINK_DOWN':'link_down','THERMAL':'thermal',
              'PSU_FAILURE':'power','FAN_FAILURE':'fan','LOOP':'loop','SOURCE_LOST':'source_lost',
              'SOURCE_ONLINE':'normal','ETH_LINK_DOWN':'link_down','ETH_LINK_UP':'normal',
              'RECOVERY':'recovery_report','RECOVERED':'recovery_report',
              'RECORD_GAP':'record_gap','TRACKING_LOST':'record_gap','SUMMARY_CAPACITY':'record_gap',
              'LOGIN_FAILURE':'login_failure','STP_PROTECTION':'stp_protection',
              'MAC_MOVE':'mac_move','CPU_HIGH':'resource_pressure'}
    for event, label in events.items():
        for variation in range(24):
            variables=values()
            status='OK' if label in ('normal','recovery_report') else 'ALARM'
            patterns=[f'H3C LOG device={{device}} component={{port}} event={event} status={status}',
                      f'event=DETECTED fault={event} severity=CRITICAL',
                      f'event={event} device={{device}}',
                      f'H3C LOG event={event} status={status} level=WARN component={{port}} device={{device}}']
            # Normalized summary uses DETECTED/fault only for actual fault classes.
            if label in ('normal','recovery_report','record_gap','source_lost'):
                patterns[1]=f'event={event} source=esp_observation'
            text=patterns[variation%len(patterns)].format(**variables)
            sets['train'].append({'label':label,'template_group':'normalized:'+event,
                                  'kind':'synthetic_protocol_adapter','text':text})
    # Template names alone are insufficient: stripping IDs can make two different
    # templates identical to the model. Preserve test groups; exclude whole
    # overlapping validation/train groups, rather than leaking numeric variants.
    test_signatures={tuple(features(row['text'])) for row in sets['test']}
    invalid_validation={row['template_group'] for row in sets['validation']
                        if tuple(features(row['text'])) in test_signatures}
    sets['validation']=[row for row in sets['validation'] if row['template_group'] not in invalid_validation]
    heldout=test_signatures | {tuple(features(row['text'])) for row in sets['validation']}
    invalid_train={row['template_group'] for row in sets['train'] if tuple(features(row['text'])) in heldout}
    sets['train']=[row for row in sets['train'] if row['template_group'] not in invalid_train]
    dataset.excluded={'validation':sorted(invalid_validation),'train':sorted(invalid_train)}
    return labels, sets


def evaluate(labels, rows, weights, bias, threshold):
    confusion = {label: {} for label in labels}
    correct = accepted = accepted_correct = known = 0
    for row in rows:
        values = probabilities(weights, bias, features(row['text'],len(weights[0])))
        predicted = labels[max(range(len(labels)), key=values.__getitem__)]
        actual = row['label']
        correct += predicted == actual
        confusion[actual][predicted] = confusion[actual].get(predicted, 0) + 1
        if actual != 'unknown':
            known += 1
        if max(values) >= threshold and predicted != 'unknown':
            accepted += 1
            accepted_correct += predicted == actual
    return {'samples': len(rows), 'groups':len({row['template_group'] for row in rows}), 'accuracy': correct / len(rows),
            'accepted': accepted, 'coverage': accepted / len(rows),
            'accepted_accuracy': accepted_correct / accepted if accepted else None,
            'known_samples': known, 'confusion': confusion}


def main():
    labels, sets = dataset()
    train = [(labels.index(row['label']), features(row['text'])) for row in sets['train']]
    rng = random.Random(SEED)
    weights = [[0.0] * DIM for _ in labels]
    bias = [0.0] * len(labels)
    for epoch in range(110):
        rng.shuffle(train)
        rate = 0.35 / (1 + epoch * 0.025)
        for expected, indices in train:
            values = probabilities(weights, bias, indices)
            scale = 1 / math.sqrt(max(1, len(indices)))
            for category, row in enumerate(weights):
                gradient = values[category] - (category == expected)
                bias[category] -= rate * gradient
                for index in indices:
                    row[index] -= rate * (gradient * scale + 0.0001 * row[index])
    # Choose abstention threshold ONLY with validation. Test is evaluated once below.
    threshold = 1.0  # Fail closed if no validation operating point passes.
    for candidate in (0.90, 0.95, 0.98, 0.99):
        metric = evaluate(labels, sets['validation'], weights, bias, candidate)
        if metric['accepted_accuracy'] is not None and metric['accepted_accuracy'] == 1.0:
            threshold = candidate
            break
    model = {'version': VERSION, 'seed': SEED, 'dimension': DIM,
             'labels': labels, 'threshold': threshold, 'weights': weights, 'bias': bias}
    output = ROOT / 'artifacts'
    output.mkdir(exist_ok=True)
    (output / 'model.json').write_text(json.dumps(model, separators=(',', ':')) + '\n')
    (output / 'split.json').write_text(json.dumps(sets, indent=2) + '\n')
    corpus_paths=[ROOT/'samples.json', ROOT/'corpus/additional.json', ROOT/'corpus/h3c.json', ROOT/'corpus/sources.json']
    reports = {'model':VERSION,'data': 'official facts with authored paraphrases + synthetic templates; NOT real-switch accuracy', 'seed': SEED,
               'split': 'synthetic whole templates 6/2/2; official whole mnemonic families; protocol adapters train only',
               'official_sources':len(json.loads((ROOT/'corpus/sources.json').read_text())['sources']),
               'official_authored_paraphrases':len(json.loads((ROOT/'corpus/h3c.json').read_text())),
               'excluded_feature_duplicate_groups':dataset.excluded,
               'labels': labels, 'threshold_from_validation': threshold,
               'float32_parameter_bytes': (DIM + 1) * len(labels) * 4,
               'dataset_sha256': hashlib.sha256(b''.join(path.read_bytes() for path in corpus_paths)).hexdigest(),
               'limitations':['Synthetic development evaluation, not real-device incidents or blind testing.',
                              'Template expansions are correlated; group counts matter more than row counts.',
                              'Documentation spans several H3C platforms; exact-release support is unverified.']}
    for name, rows in sets.items():
        reports[name] = evaluate(labels, rows, weights, bias, threshold)
    reports['official_holdout']={name:evaluate(labels,[row for row in sets[name] if row.get('source_id')],weights,bias,threshold) for name in ('validation','test')}
    # Same frozen v1 template test set, same labels: an explicit comparable metric.
    baseline_path=ROOT/'baseline/v1-model.json'
    if baseline_path.exists():
        baseline=json.loads(baseline_path.read_text())
        original_labels=list(json.loads((ROOT/'samples.json').read_text()))
        original=[row for row in sets['test'] if row['template_group'].split(':')[0] in original_labels]
        reports['v1_template_comparison']={
            'baseline':evaluate(baseline['labels'],original,baseline['weights'],baseline['bias'],baseline['threshold']),
            'current':evaluate(labels,original,weights,bias,threshold)}
    (output / 'metrics.json').write_text(json.dumps(reports, indent=2) + '\n')
    def floats(values):
        return ','.join(f'{v:.8e}f' for v in values)
    header = '// Generated by train.py. Official-derived + synthetic classifier; display only.\n#pragma once\nnamespace tinylog {\n'
    header += f'static const char VERSION[]={json.dumps(VERSION)};\n'
    header += f'constexpr unsigned DIM={DIM}, CLASSES={len(labels)};\n'
    header += f'constexpr float THRESHOLD={threshold:.8e}f;\n'
    header += 'static const char* const LABELS[]={' + ','.join(json.dumps(x) for x in labels) + '};\n'
    header += 'static const float BIAS[CLASSES]={' + floats(bias) + '};\n'
    header += 'static const float WEIGHTS[CLASSES][DIM]={\n'
    header += ',\n'.join('{' + floats(row) + '}' for row in weights) + '\n};\n}\n'
    (output / 'model.h').write_text(header)
    # Parity fixtures include unseen templates and boundary cases; no ground truth invented.
    fixtures = [row['text'] for row in sets['test'][::10]] + ['', '????', 'x' * 1500,
               'event=DETECTED fault=THERMAL device=sw1 component=TEMP1',
               'H3C ACK action=UNLOCK result=SUCCESS fault=AUTH_LOCKED']
    (output / 'parity.json').write_text(json.dumps([
        {'text': text, 'scores': probabilities(weights, bias, features(text))}
        for text in fixtures], indent=2) + '\n')
    print(json.dumps({name: {k: v for k, v in reports[name].items() if k != 'confusion'}
                      for name in sets}, indent=2))
    print(f'threshold={threshold}; parameters={reports["float32_parameter_bytes"]} bytes')


if __name__ == '__main__':
    main()
