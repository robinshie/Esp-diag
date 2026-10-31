#!/usr/bin/env python3
"""Authorized lab upload + USB inference parity. Never injects a BDI log/action."""
import argparse
import json
import os
import subprocess
import time
from pathlib import Path

import serial
from train import ROOT, features, probabilities


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--flash', action='store_true')
    parser.add_argument('--port', default='/dev/cu.usbmodem101')
    args = parser.parse_args()
    repo = ROOT.parents[1]
    python = str(repo / '.venv/bin/python')
    demo = str(repo / 'test/live_demo.py')
    model = json.loads((ROOT / 'artifacts/model.json').read_text())
    cases = [
        'event=DETECTED fault=AUTH_LOCKED',
        'event=ETH_LINK_DOWN',
        '[BDI][ACK] device=sw1 action=UNLOCK result=SUCCESS request_id=r1',
        'event=DETECTED fault=THERMAL severity=CRITICAL',
        'H3C LOG device=sw1 event=RECOVERY status=OK fault=AUTH_LOCKED',
        'unknown alarm unsupported fault abc',
        '????',
        'IFNET/3/PHY_UPDOWN: Interface GigabitEthernet1/0/8 physical carrier became down',
        'IFNET/3/PHY_UPDOWN: Interface GigabitEthernet1/0/8 physical carrier became up',
        'DEV/2/FAN_FAILED: Cooling fan 2 stopped unexpectedly',
        'DEV/2/POWER_FAILED: Power module 1 reports failure',
        'DEV/4/TEMPERATURE_WARNING: Sensor 1 temperature crossed the warning boundary',
        'LPDT/4/LPDT_LOOPED: Local loop detection reports a loop at GigabitEthernet1/0/8',
        'STP/4/STP_LOOP_PROTECTION: Loop guard missed configuration BPDU packets',
        'MAC/4/MAC_FLAPPING: A learned MAC address moved between ports',
        'AAA/5/AAA_FAILURE: AAA service rejected the requested user access',
        'DIAG/4/CPU_MINOR_THRESHOLD: CPU utilization crossed its minor alarm boundary',
        'DIAG/5/CPU_MINOR_RECOVERY: CPU utilization fell into the recovery interval',
    ]
    semantic=json.loads((ROOT/'corpus/qualification.json').read_text())
    expected_labels={row['text']:row['label'] for row in semantic}
    cases.extend(expected_labels)
    state_file = repo / 'test/.runtime/live-demo/processes.json'
    state = json.loads(state_file.read_text()) if state_file.exists() else {}
    gateway = state.get('gateway', {}).get('pid')
    restore_gateway = False
    if gateway:
        try:
            os.kill(gateway, 0)
            restore_gateway = True
        except ProcessLookupError:
            pass
    report = {'model': model['version'], 'display_only': True, 'flashed':args.flash,
              'corpus_sha256':json.loads((ROOT/'artifacts/metrics.json').read_text())['dataset_sha256'],
              'cases': [], 'status': []}
    try:
        if restore_gateway:
            subprocess.run([python, demo, 'stop-gateway'], cwd=repo, check=True)
        if args.flash:
            env = dict(os.environ, PLATFORMIO_CORE_DIR=str(repo / '.pio-core'),
                       PLATFORMIO_SETTING_ENABLE_TELEMETRY='no')
            subprocess.run([str(Path.home() / '.platformio/penv/bin/pio'), 'run',
                            '-e', 'esp32-s3-log-ai-lab', '--target', 'upload',
                            '--upload-port', args.port], cwd=repo, env=env, check=True)
        with serial.Serial(args.port, 115200, timeout=0.3) as connection:
            connection.dtr = True
            connection.rts = False
            def read_until(deadline, prediction=False):
                while time.monotonic() < deadline:
                    line = connection.readline().decode('utf-8', errors='replace').strip()
                    if '[BDI][STATUS]' in line:
                        report['status'].append(line)
                    if prediction and line.startswith('[LOG-AI] {'):
                        return json.loads(line[len('[LOG-AI] '):])
                return None
            read_until(time.monotonic() + 8)
            for text in cases:
                start = time.monotonic()
                connection.write(('H3C AI ' + text + '\n').encode())
                connection.flush()
                reply = read_until(time.monotonic() + 4, prediction=True)
                assert reply is not None, f'No reply for {text}'
                scores = probabilities(model['weights'], model['bias'], features(text))
                category = max(range(len(scores)), key=scores.__getitem__)
                expected = model['labels'][category]
                accepted = len(features(text)) >= 2 and max(scores) >= model['threshold'] and expected != 'unknown'
                assert reply['model'] == model['version'] and reply['experimental'] is True
                assert reply['label'] == expected and reply['accepted'] == accepted, (text, reply)
                if text in expected_labels and reply['accepted']:
                    assert reply['label']==expected_labels[text], ('semantic misclassification',text,reply)
                assert abs(reply['score'] - max(scores)) < 0.00002, (text, reply)
                report['cases'].append({'text': text, 'reply': reply,
                                        'roundtrip_ms': round((time.monotonic() - start) * 1000, 2)})
            # Exercise repeated inference without changing state or filling TF with test faults.
            for _ in range(40):
                connection.write(b'H3C AI event=DETECTED fault=AUTH_LOCKED\n')
                reply = read_until(time.monotonic() + 3, prediction=True)
                assert reply and reply['label'] == 'auth_lock' and reply['accepted']
            report['repeated_predictions'] = 40
            read_until(time.monotonic() + 6)
            assert report['status'] and 'sd_state=ready' in report['status'][-1]
            report['passed'] = True
    finally:
        destination = repo / 'test/.runtime/tiny-log-hardware.json'
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
        if restore_gateway:
            subprocess.run([python, demo, 'restart-gateway'], cwd=repo, check=True)
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
