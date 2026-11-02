"""Copy database-committed reports to SD. Never uploads SD records to MongoDB."""
import base64
import hashlib
import json
import logging
import secrets
import threading
import time
from urllib.parse import quote
from urllib.request import Request, urlopen

from .config import credential
from .store import fields

log = logging.getLogger(__name__)


class ArchiveCopy:
    def __init__(self, gateway, wall_clock=time.time):
        self.gateway = gateway
        self.wall_clock = wall_clock
        self.condition = threading.Condition()
        self.reply = None
        self.request = None
        self.supported = False
        with gateway.store.lock, gateway.store.db:
            gateway.store.db.execute('CREATE TABLE IF NOT EXISTS sd_copy_cursor(agent TEXT PRIMARY KEY, sequence INTEGER)')
            gateway.store.db.execute('CREATE TABLE IF NOT EXISTS sd_copy_schedule(agent TEXT PRIMARY KEY, committed_at_ms INTEGER)')
            gateway.store.db.execute('CREATE TABLE IF NOT EXISTS sd_copy_start(agent TEXT PRIMARY KEY, received_after_ms INTEGER)')

    def accept(self, line):
        if not line.startswith('[SDCOPY] '):
            return False
        with self.condition:
            reply = fields(line)
            if reply.get('req') == self.request:
                self.reply = reply
                self.condition.notify_all()
        return True

    def exchange(self, command, key):
        with self.condition:
            self.request = secrets.token_hex(8)
            self.reply = None
            frame = f'H3C COPY req={self.request} key={key} {command}'
            if len(frame.encode()) > 511:
                raise ValueError('SD copy frame capacity exceeded')
            if not self.gateway.send_usb(frame):
                raise ConnectionError('USB unavailable')
            self.condition.wait_for(lambda: self.reply is not None or self.gateway.stop_event.is_set(), timeout=10)
            reply = self.reply
        if not reply or reply.get('key') != key:
            raise TimeoutError('SD copy acknowledgement missing')
        if reply.get('state') not in {'ready', 'committed'}:
            raise OSError('SD copy not ready: ' + reply.get('state', 'unknown'))
        return reply

    def transfer(self, item, card_id):
        payload = item['payload'].encode('utf-8')
        key = hashlib.sha256(payload).hexdigest()
        if key != item['sha256'] or not 0 < len(payload) <= 2 * 1024 * 1024:
            raise ValueError('Invalid database copy digest/size')
        reply = self.exchange(f"op=begin seq={item['sequence']} size={len(payload)}", key)
        if reply.get('card') != card_id:
            raise ConnectionError('SD card identity changed')
        if reply['state'] != 'committed':
            offset = int(reply.get('next', 0))
            while offset < len(payload):
                if self.gateway.stop_event.is_set() or not self.gateway.pong_seen.is_set():
                    raise ConnectionError('USB stopped during SD copy')
                chunk = payload[offset:offset + 240]
                reply = self.exchange(f"op=data offset={offset} data={base64.b64encode(chunk).decode()}", key)
                expected = offset + len(chunk)
                if int(reply.get('next', -1)) != expected:
                    raise ValueError('SD copy offset mismatch')
                offset = expected
                self.gateway.stop_event.wait(.005)
            reply = self.exchange('op=end', key)
            if reply['state'] != 'committed':
                raise OSError('SD copy was not committed')
        # Only a digest-verified, closed SD file can advance this durable cursor.
        with self.gateway.store.lock, self.gateway.store.db:
            self.gateway.store.db.execute('INSERT OR REPLACE INTO sd_copy_cursor VALUES(?,?)',
                                          (self.gateway.agent_topic + '/' + card_id, item['sequence']))
            self.gateway.store.db.execute('INSERT OR REPLACE INTO sd_copy_schedule VALUES(?,?)',
                                          (self.gateway.agent_topic + '/' + card_id, round(self.wall_clock() * 1000)))

    def fetch(self, config, suffix):
        url = config['api_url'].rstrip('/') + '/devices/' + quote(self.gateway.agent_topic.split('/')[-1], safe='') + '/sd-copy' + suffix
        with urlopen(Request(url, headers={'Authorization': 'Bearer ' + credential(config, 'token_env')}), timeout=5) as response:
            return json.loads(response.read(5 * 1024 * 1024))

    def start_boundary(self, config, card_id):
        if config.get('backfill_history', False):
            return None
        identity = self.gateway.agent_topic + '/' + card_id
        with self.gateway.store.lock:
            row = self.gateway.store.db.execute('SELECT received_after_ms FROM sd_copy_start WHERE agent=?', (identity,)).fetchone()
        if row:
            return row[0]
        # Use the database server's clock. Persist once per card, including
        # migration from the former unlimited historical backfill policy.
        boundary = self.fetch(config, '/boundary')['received_after_ms']
        if type(boundary) is not int or boundary < 0:
            raise ValueError('Invalid SD copy start boundary')
        with self.gateway.store.lock, self.gateway.store.db:
            self.gateway.store.db.execute('INSERT OR IGNORE INTO sd_copy_start VALUES(?,?)', (identity, boundary))
        return boundary

    def remaining_delay(self, card_id):
        reports = getattr(self.gateway, 'reports', None)
        interval = reports.interval if reports is not None else getattr(self.gateway.args, 'report_interval_ms', 60000) / 1000
        with self.gateway.store.lock:
            row = self.gateway.store.db.execute('SELECT committed_at_ms FROM sd_copy_schedule WHERE agent=?',
                                               (self.gateway.agent_topic + '/' + card_id,)).fetchone()
        return max(0, (row[0] / 1000 + interval - self.wall_clock())) if row else 0

    def run(self):
        config = getattr(self.gateway.args, 'archive_copy', None)
        if not config:
            return
        previous_error = None
        while not self.gateway.stop_event.wait(.2):
            if not self.supported or not self.gateway.pong_seen.is_set():
                continue
            try:
                status = self.exchange('op=status', '0' * 64)
                card_id = status.get('card', '')
                if len(card_id) != 32 or any(c not in '0123456789abcdef' for c in card_id):
                    raise ValueError('SD identity unavailable')
                boundary = self.start_boundary(config, card_id)
                delay = self.remaining_delay(card_id)
                if delay:
                    self.gateway.stop_event.wait(min(delay, 2))
                    continue
                with self.gateway.store.lock:
                    row = self.gateway.store.db.execute('SELECT sequence FROM sd_copy_cursor WHERE agent=?',
                                                       (self.gateway.agent_topic + '/' + card_id,)).fetchone()
                after = row[0] if row else 0
                suffix = f'?after={after}'
                if boundary is not None:
                    suffix += f'&received_after_ms={boundary}'
                item = self.fetch(config, suffix).get('copy')
                if item:
                    self.transfer(item, card_id)
                else:
                    self.gateway.stop_event.wait(2)
                previous_error = None
            except Exception as error:
                name = type(error).__name__
                if name != previous_error:
                    log.warning('Database to SD copy pending: %s; cursor preserved', name)
                previous_error = name
                self.gateway.stop_event.wait(5)
