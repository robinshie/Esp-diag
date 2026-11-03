#!/usr/bin/env python3
"""One confirmed owned restore, then read-only verification. Never changes Wi-Fi."""
import argparse
import json
from pathlib import Path
import time
from urllib.parse import urlencode
from urllib.request import Request, ProxyHandler, build_opener

BASE = 'http://192.168.4.1'
VERSION = 'native-bdi-20261103-r18-storm2-lab'
PORT = 'GigabitEthernet1/0/2'
TABLES = ('interfaces', 'down', 'loops', 'stp', 'aggregation', 'traffic')


def recovery(status):
    rows = [r for r in status.get('bdi', {}).get('recovery', []) if r.get('port') == PORT]
    if len(rows) != 1:
        raise ValueError('Expected exactly one owned port-2 recovery record')
    return rows[0]


def validate(status, boot=None, before_restore=False):
    if status.get('firmware_version') != VERSION or not status.get('boot'):
        raise ValueError('Expected r18 storm-2 acceptance firmware')
    if boot is not None and status['boot'] != boot:
        raise ValueError('Device rebooted; do not replay restore')
    if (status.get('ssh_state') != 'connected'
            or status.get('management', {}).get('session_accepted') is not True
            or status.get('management', {}).get('transport') != 'ssh'
            or status.get('ssh_host_key_state') != 'trusted'):
        raise ValueError('Trusted SSH session is unavailable')
    heartbeat = status.get('heartbeat', '')
    if 'heartbeat_state=healthy' not in heartbeat or 'heartbeat_misses=0' not in heartbeat:
        raise ValueError('Management heartbeat is not healthy')
    lab = status.get('storm_acceptance', {})
    if lab.get('port') != PORT or lab.get('required_vlan') != 4090 or lab.get('test_threshold_pps') != 20:
        raise ValueError('Unexpected experiment scope')
    if lab.get('state') in ('needs_review', 'blocked'):
        raise ValueError('Experiment requires review: ' + str(lab.get('reason')))
    row = recovery(status)
    if row.get('state') == 'needs_review':
        raise ValueError('Fault runtime requires review: ' + str(row.get('reason')))
    if before_restore:
        if (lab.get('state') != 'awaiting_stop_confirmation'
                or row.get('state') != 'contained' or row.get('owned') is not True
                or status.get('bdi', {}).get('enabled') is not True):
            raise ValueError('Port 2 is not awaiting a confirmed owned restore')
    return row


def completed(status, boot):
    row = validate(status, boot)
    bdi = status['bdi']
    if row.get('state') != 'recovered' or row.get('owned') is not False or status['storm_acceptance'].get('state') != 'passed':
        return False
    cycle = bdi.get('cycle')
    if type(cycle) is not int or cycle <= 0 or bdi.get('observation_valid') is not True:
        return False
    for name in TABLES:
        table = bdi.get('tables', {}).get(name, {})
        if table.get('cycle') != cycle or any(table.get(k) is not True for k in ('valid', 'complete', 'fresh')):
            return False
    return bdi.get('enabled') is False


def run(get, post, output, confirmed, clock=time.monotonic, pause=time.sleep):
    result = {'kind': 'real_storm_owned_recovery', 'passed': False,
              'operator_confirmed_sender_stopped': confirmed, 'restore_posts': 0,
              'statuses': [], 'errors': []}
    try:
        if not confirmed:
            raise ValueError('Confirm Windows says SENDER STOPPED before requesting restore')
        first = get()
        result['statuses'].append(first)
        validate(first, before_restore=True)
        boot = first['boot']
        # Exactly one attempt. An HTTP timeout is ambiguous and must not replay.
        result['restore_posts'] = 1
        answer = post()
        result['restore_answer'] = answer
        if answer != 'queued':
            raise ValueError('Restore was not queued: ' + answer)
        end = clock() + 180
        while clock() < end:
            pause(3)
            status = get()
            result['statuses'].append(status)
            if completed(status, boot):
                result['passed'] = True
                break
        if not result['passed']:
            raise TimeoutError('Recovery verification deadline; use Console to review')
    except Exception as error:
        result['errors'].append(type(error).__name__ + ': ' + str(error))
    finally:
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists():
            output.with_name(output.stem + '-' + str(time.time_ns()) + output.suffix).write_bytes(output.read_bytes())
        output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--confirm-generator-stopped', action='store_true')
    parser.add_argument('--output', type=Path, default=Path('test/.runtime/storm2-real-recovery.json'))
    args = parser.parse_args()
    opener = build_opener(ProxyHandler({}))

    def get():
        with opener.open(BASE + '/device/status', timeout=5) as response:
            return json.loads(response.read(32768))

    def post():
        request = Request(BASE + '/switch/restore', method='POST',
                          data=urlencode({'port': PORT, 'confirm': 'repair'}).encode(),
                          headers={'Origin': BASE, 'Content-Type': 'application/x-www-form-urlencoded'})
        with opener.open(request, timeout=5) as response:
            return response.read(4096).decode().strip()

    result = run(get, post, args.output, args.confirm_generator_stopped)
    print(json.dumps({'passed': result['passed'], 'restore_posts': result['restore_posts'],
                      'errors': result['errors'], 'report': str(args.output)}, ensure_ascii=False))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
