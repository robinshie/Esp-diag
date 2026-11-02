#!/usr/bin/env python3
"""Offline, read-only ESP HTTP collection; no GPT, USB or third-party packages."""
import argparse
import base64
import json
from pathlib import Path
import time
from urllib.parse import parse_qs, urlencode, urlsplit
from urllib.request import build_opener, ProxyHandler


MAX_REPORT_BYTES = 65536


def read_report(get, filename, pause=time.sleep):
    data = bytearray()
    size = None
    for _ in range(700):
        offset = len(data)
        reply = get('/storage/read?' + urlencode({'file': filename, 'offset': offset}))
        job = reply.get('job')
        for _ in range(80):
            if reply.get('state') not in ('pending', 'busy'):
                break
            pause(.1)
            reply = get('/storage/read?' + urlencode({'job': job} if job else {'file': filename, 'offset': offset}))
            job = reply.get('job', job)
        if reply.get('state') != 'ready':
            raise ValueError('日志读取未完成：' + str(reply.get('state')))
        chunk = base64.b64decode(reply['base64'], validate=True)
        current_size = reply['size']
        if (type(current_size) is not int or not 0 < current_size <= MAX_REPORT_BYTES
                or (size is not None and current_size != size)
                or reply['start'] != offset or reply['next'] != offset + len(chunk)
                or reply['next'] > current_size or not chunk
                or bool(reply['eof']) != (reply['next'] == current_size)):
            raise ValueError('日志分片位置或长度异常')
        size = current_size
        data.extend(chunk)
        if reply['eof']:
            report = json.loads(data.decode('utf-8'))
            if (report.get('schema_version') != 1 or report.get('source') != 'esp_native'
                    or not isinstance(report.get('lines'), list) or len(report['lines']) > 64):
                raise ValueError('不是有效的 ESP 原生日志包')
            return report
    raise ValueError('日志分片过多')


def summarize(bundle):
    """Keep command, heartbeat and policy evidence separate; never infer repair."""
    statuses = bundle['statuses']
    current = statuses[-1] if statuses else {}
    boot = current.get('boot')
    reports = [r for r in bundle['reports'] if boot and r.get('boot') == boot]
    lines = [line.get('text', '') for report in reports for line in report['lines']]
    heartbeat = current.get('heartbeat', '')
    healthy = ('heartbeat_state=healthy' in heartbeat if isinstance(heartbeat, str)
               else heartbeat.get('state') == 'healthy' if isinstance(heartbeat, dict) else False)
    commands = sorted({text.split(' command=', 1)[1].split()[0] for text in lines
                       if text.startswith('[SSH][COMMAND]') and ' command=' in text and ' state=ok' in text})
    bdi = current.get('bdi', {})
    return {'ssh_connected': current.get('ssh_state') == 'connected',
            'heartbeat_healthy': healthy, 'commands_ok_current_boot': commands,
            'bdi_observation_valid': bdi.get('observation_valid') is True,
            'bdi_state': bdi.get('state'), 'bdi_reason': bdi.get('reason'),
            'loop_detection_enabled': bdi.get('loop_detection_enabled'),
            'bdi_cycle': bdi.get('cycle'), 'reports_current_boot': len(reports),
            'note': '只读采集；动作执行和物理环路恢复须另核对事件、端口及连续读回。'}


def collect(get, output, wait=600, observe=30, limit=40, pause=time.sleep, clock=time.monotonic):
    bundle = {'schema_version': 1, 'kind': 'esp_offline_diagnostics',
              'statuses': [], 'reports': [], 'errors': [], 'storage_samples': [],
              'listing_samples': [], 'collection_complete': False}
    # Preserve the previous run, including evidence that this attempt may miss.
    if output.exists():
        backup = output.with_name(output.stem + '-' + str(time.time_ns()) + output.suffix)
        backup.write_bytes(output.read_bytes())

    def save():
        bundle['summary'] = summarize(bundle)
        output.parent.mkdir(parents=True, exist_ok=True)
        temporary = output.with_suffix(output.suffix + '.tmp')
        temporary.write_text(json.dumps(bundle, ensure_ascii=False, indent=2), encoding='utf-8')
        temporary.replace(output)

    deadline = clock() + wait
    print('等待 ESP 热点。现在可以离线：三击 BOOT，连接 ESP-Logs 热点。', flush=True)
    while True:
        try:
            status = get('/device/status')
            if status.get('mode') != 'standalone' or status.get('log_source') != 'esp_native':
                raise ValueError('目标不是正式 ESP 设备')
            bundle['statuses'].append(status)
            save()
            break
        except (OSError, ValueError) as error:
            if clock() >= deadline:
                bundle['errors'].append('等待设备超时：' + str(error))
                save()
                return False
            pause(2)
    print('已连接，正在保存状态和 TF 日志；请保持热点连接。', flush=True)

    def storage_status():
        try:
            bundle['storage_samples'].append(get('/storage'))
        except (OSError, ValueError) as error:
            bundle['storage_samples'].append({'error': str(error)})
        save()

    try:
        storage_status()
        end = clock() + observe
        while clock() < end:
            pause(min(5, end - clock()))
            bundle['statuses'].append(get('/device/status'))
            save()
        before, seen = 0, set()
        while len(bundle['reports']) < limit:
            listing = None
            # Continuous SSH output can keep the firmware's flush-before-list
            # endpoint busy longer than the original eight-second retry window.
            for attempt in range(121):
                listing = get('/storage/files?' + urlencode({'before': before}))
                sample = {'before': before, 'response': listing}
                if not bundle['listing_samples'] or bundle['listing_samples'][-1] != sample:
                    bundle['listing_samples'].append(sample)
                    save()
                if listing.get('state') not in ('busy', 'starting'):
                    break
                if attempt % 20 == 0:
                    print('等待 TF 列表：' + str(listing.get('state')), flush=True)
                    storage_status()
                    bundle['statuses'].append(get('/device/status'))
                    save()
                if attempt < 120:
                    pause(.5)
            if listing.get('state') != 'ready' or listing.get('source') != 'esp_native':
                storage_status()
                raise ValueError('TF 日志列表未就绪，原始响应：' + json.dumps(listing, ensure_ascii=False))
            files = listing.get('files', [])
            for item in files:
                if len(bundle['reports']) >= limit:
                    break
                if item['name'] in seen:
                    raise ValueError('日志分页重复')
                seen.add(item['name'])
                try:
                    bundle['reports'].append(read_report(get, item['name'], pause))
                    print('已保存日志 ' + str(len(bundle['reports'])) + '/' + str(limit), flush=True)
                except (OSError, ValueError, KeyError, TypeError) as error:
                    bundle['errors'].append({'file': item['name'], 'error': str(error)})
                save()
            if not files or not listing.get('has_older'):
                break
            next_before = files[-1]['sequence']
            if type(next_before) is not int or next_before <= 0 or (before and next_before >= before):
                raise ValueError('日志分页游标异常')
            before = next_before
        bundle['statuses'].append(get('/device/status'))
        storage_status()
        bundle['collection_complete'] = bool(bundle['reports']) and not bundle['errors']
        if not bundle['reports']:
            bundle['errors'].append('没有读到日志包')
    except (OSError, ValueError, KeyError, TypeError) as error:
        bundle['errors'].append(str(error))
        print('采集遇到问题：' + str(error), flush=True)
        try:
            bundle['statuses'].append(get('/device/status'))
        except (OSError, ValueError) as status_error:
            bundle['errors'].append('末次状态读取失败：' + str(status_error))
    save()
    print(('采集完成' if bundle['collection_complete'] else '已保存部分结果') + '：' + str(output), flush=True)
    print('现在可切回有互联网的 Wi-Fi，让 GPT 读取此文件。', flush=True)
    return bundle['collection_complete']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://192.168.4.1')
    parser.add_argument('--wait', type=float, default=600, help='等待切换热点的秒数')
    parser.add_argument('--observe', type=float, default=30, help='前后状态采样时长')
    parser.add_argument('--reports', type=int, default=40)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'test/.runtime/esp-offline-diagnostics.json')
    args = parser.parse_args()
    if not (0 < args.wait <= 3600 and 0 <= args.observe <= 120 and 1 <= args.reports <= 100):
        parser.error('等待 1–3600 秒、观察 0–120 秒、日志 1–100 包')
    base = args.url.rstrip('/')
    parsed = urlsplit(base)
    if parsed.scheme != 'http' or not parsed.hostname or parsed.username or parsed.password or parsed.path or parsed.query or parsed.fragment:
        parser.error('--url 必须是设备 HTTP 地址，不含路径或凭据')
    opener = build_opener(ProxyHandler({}))

    def get(path):
        with opener.open(base + path, timeout=5) as response:
            raw = response.read(131073)
            if len(raw) > 131072:
                raise ValueError('设备响应过大')
            text = raw.decode('utf-8')
            if path == '/storage':
                return {key: values[-1] for key, values in parse_qs(text).items()}
            return json.loads(text)

    return 0 if collect(get, args.output, args.wait, args.observe, args.reports) else 1


if __name__ == '__main__':
    raise SystemExit(main())
