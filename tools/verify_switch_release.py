#!/usr/bin/env python3
"""Offline HTTP-only firmware acceptance with optional temporary Wi-Fi join."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
from urllib.error import HTTPError
from urllib.parse import parse_qs
from urllib.request import ProxyHandler, build_opener

from collect_device_logs import collect

ROOT = Path(__file__).resolve().parents[1]
READS = {'version', 'interfaces', 'down', 'loops', 'stp', 'aggregation',
         'logs', 'cpu', 'memory', 'mac_moves', 'traffic'}
TABLES = {'interfaces', 'down', 'loops', 'stp', 'aggregation', 'traffic'}


def complete_cycle(device):
    bdi = device.get('bdi', {})
    return bdi.get('observation_valid') is True and all(
        all(bdi.get('tables', {}).get(name, {}).get(key) is True
            for key in ('valid', 'complete', 'fresh'))
        and bdi['tables'][name].get('cycle') == bdi.get('cycle')
        for name in TABLES)


def validate(bundle, version):
    statuses = bundle.get('statuses', [])
    boots = {s.get('boot') for s in statuses}
    cycles = sorted({s['bdi']['cycle'] for s in statuses if complete_cycle(s)})
    boot = next(iter(boots)) if len(boots) == 1 else None
    lines = [line.get('text', '') for report in bundle.get('reports', [])
             if boot and report.get('boot') == boot for line in report.get('lines', [])]
    commands = {line.split(' command=', 1)[1].split()[0]
                for line in lines if line.startswith('[SSH][COMMAND]')
                and ' command=' in line and ' state=ok' in line}
    truncated = {line.split(' command=', 1)[1].split()[0]
                 for line in lines if line.startswith('[SSH][COMMAND]')
                 and ' command=' in line and ' state=truncated' in line}
    writes = [line for line in lines if line.startswith('[BDI][CMD]') or
              (line.startswith('[SSH][COMMAND]') and any(
                  ' command=' + prefix in line for prefix in ('isolate:', 'storm-isolate:', 'restore:')))]
    checks = {
        'collection_complete': bundle.get('collection_complete') is True,
        'single_boot': len(boots) == 1 and bool(boot),
        'current_firmware': bool(statuses) and all(s.get('firmware_version') == version for s in statuses),
        'readonly_firmware': bool(statuses) and all(s.get('acceptance_read_only') is True and
                                                  s.get('bdi', {}).get('enabled') is False for s in statuses),
        'ssh_stable': bool(statuses) and all(s.get('ssh_state') == 'connected' for s in statuses),
        'heartbeat_stable': bool(statuses) and all('heartbeat_state=healthy' in s.get('heartbeat', '') for s in statuses),
        'strategies_loaded': bool(statuses) and all(set(s.get('bdi', {}).get('strategy_ids', [])) ==
                                                     {'loop_discovery', 'loop_isolation', 'storm_isolation'} for s in statuses),
        'storm_disabled_without_thresholds': bool(statuses) and all(s.get('bdi', {}).get('storm', {}).get('state') == 'disabled' for s in statuses),
        'multiple_complete_cycles': len(cycles) >= 2,
        'core_named_reads_ok': (READS - {'logs'}) <= commands,
        'log_read_completed': 'logs' in commands | truncated,
        'no_captured_write_commands': not writes,
    }
    runtime_checks = {k: v for k, v in checks.items() if k != 'collection_complete'}
    return {'passed': all(checks.values()), 'runtime_passed': all(runtime_checks.values()),
            'checks': checks, 'boot': boot,
            'complete_cycles': cycles, 'status_samples': len(statuses),
            'commands_ok_current_boot': sorted(commands), 'captured_write_commands': writes,
            'commands_truncated_current_boot': sorted(truncated),
            'warnings': ['Switch logbuffer exceeded the bounded SSH output; core state tables remain independently validated.'] if 'logs' in truncated else [],
            'captured_link_events': [line for line in lines if line.startswith('[NETWORK][LINK]')],
            'captured_reset_events': [line for line in lines if line.startswith('[SYSTEM][RESET]')],
            'scope': 'Read-only HTTP and current-boot logs; no fault injection or switch writes.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://192.168.4.1')
    parser.add_argument('--version', default='native-bdi-20261103-r15-faultrecovery')
    parser.add_argument('--wait', type=float, default=120)
    parser.add_argument('--observe', type=float, default=60)
    parser.add_argument('--join-network')
    parser.add_argument('--restore-network')
    parser.add_argument('--interface', default='en0')
    args = parser.parse_args()
    if bool(args.join_network) != bool(args.restore_network):
        parser.error('Temporary join requires both --join-network and --restore-network')
    if not (1 <= args.wait <= 600 and 60 <= args.observe <= 120):
        parser.error('Wait must be 1–600 seconds; observation must be 60–120 seconds')
    out = ROOT / 'test/.runtime'
    out.mkdir(exist_ok=True)
    target = out / 'r15-hardware-acceptance.json'
    raw = out / 'r15-hardware-diagnostics.json'
    opener = build_opener(ProxyHandler({}))

    def read(path):
        with opener.open(args.url.rstrip('/') + path, timeout=5) as response:
            data = response.read(131073)
            if len(data) > 131072:
                raise ValueError('Device response too large')
            return data.decode('utf-8')

    def get(path):
        text = read(path)
        return {k: v[-1] for k, v in parse_qs(text).items()} if path == '/storage' else json.loads(text)

    def wifi(name):
        answer = subprocess.run(['/usr/sbin/networksetup', '-setairportnetwork', args.interface, name],
                                capture_output=True, text=True, timeout=30)
        if answer.returncode or 'Error' in answer.stdout or 'Could not' in answer.stdout:
            raise RuntimeError('Wi-Fi operation failed: ' + answer.stdout.strip() + answer.stderr.strip())
        return answer.stdout.strip()

    evidence = {'passed': False, 'errors': [], 'firmware': args.version,
                'uploaded_build': 'esp32-s3-readonly-acceptance'}
    artifact = ROOT / '.pio/build/esp32-s3-readonly-acceptance/firmware.bin'
    if artifact.exists():
        evidence['firmware_sha256'] = hashlib.sha256(artifact.read_bytes()).hexdigest()
    original_address = subprocess.run(['/usr/sbin/ipconfig', 'getifaddr', args.interface],
                                      capture_output=True, text=True, timeout=5).stdout.strip()
    try:
        if args.join_network:
            print('临时连接 ESP 热点；离线测试结束后将恢复原网络。', flush=True)
            evidence['join_result'] = wifi(args.join_network)
        deadline = time.monotonic() + args.wait
        print('等待 r15、SSH、心跳与六张采集表就绪。', flush=True)
        while True:
            try:
                status = get('/device/status')
                if status.get('firmware_version') != args.version or status.get('acceptance_read_only') is not True:
                    raise AssertionError('Unexpected firmware or missing read-only guard')
                if (status.get('ssh_state') == 'connected' and complete_cycle(status) and
                        'heartbeat_state=healthy' in status.get('heartbeat', '') and get('/storage').get('state') == 'ready'):
                    break
            except (OSError, ValueError):
                pass
            if time.monotonic() >= deadline:
                raise TimeoutError('Firmware/SSH/heartbeat/traffic did not become ready')
            time.sleep(2)
        print('设备就绪，开始连续观察和 TF 日志自动采集。', flush=True)
        started = time.monotonic()
        collect(get, raw, wait=5, observe=args.observe, limit=20, tail_limit=5)
        bundle = json.loads(raw.read_text())
        evidence.update(validate(bundle, args.version))
        evidence['elapsed_seconds'] = round(time.monotonic() - started, 2)
        evidence['diagnostics_file'] = str(raw)
        try:
            read('/network')
            evidence['checks']['configuration_closed'] = False
        except HTTPError as error:
            evidence['checks']['configuration_closed'] = error.code == 403
        evidence['passed'] = all(evidence['checks'].values())
        evidence['runtime_passed'] = all(value for key, value in evidence['checks'].items()
                                         if key != 'collection_complete')
    except Exception as error:
        evidence['passed'] = False
        evidence['errors'].append(type(error).__name__ + ': ' + str(error))
    finally:
        if args.restore_network:
            try:
                print('恢复原 Wi-Fi。', flush=True)
                evidence['restore_result'] = wifi(args.restore_network)
            except Exception as error:
                restored = False
                for _ in range(10):
                    address = subprocess.run(['/usr/sbin/ipconfig', 'getifaddr', args.interface],
                                             capture_output=True, text=True, timeout=5).stdout.strip()
                    if original_address and address == original_address:
                        restored = True
                        break
                    time.sleep(2)
                evidence['restore_original_address_verified'] = restored
                evidence['restore_command_warning'] = str(error)
                if not restored:
                    evidence['passed'] = False
                    evidence['errors'].append('Network restore: ' + str(error))
        if target.exists():
            backup = target.with_name(target.stem + '-' + str(time.time_ns()) + target.suffix)
            backup.write_bytes(target.read_bytes())
        evidence['firmware_passed'] = bool(evidence.get('checks')) and all(evidence['checks'].values())
        target.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + '\n')
        label = '自动验收通过' if evidence['passed'] else '固件验收通过；外围流程有错误' if evidence['firmware_passed'] else '自动验收未通过，请检查结果'
        print(label + '：' + str(target), flush=True)
        print(json.dumps({k: evidence[k] for k in ('passed', 'errors', 'checks') if k in evidence}, ensure_ascii=False), flush=True)
    return 0 if evidence['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
