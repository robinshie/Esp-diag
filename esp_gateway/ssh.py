"""Bounded SSH I/O and configuration-driven state checks."""
from __future__ import annotations

import re
import threading
import time
from contextlib import contextmanager

import paramiko

from .config import credential


class CheckError(RuntimeError):
    pass


def evaluate(check, output):
    alarm = bool(re.search(check["alarm_pattern"], output))
    healthy = bool(re.search(check["healthy_pattern"], output))
    if alarm == healthy:
        raise CheckError("ambiguous or unrecognized command output")
    cause = check.get("cause", "")
    if alarm and check.get("action_when_pattern") and re.search(check["action_when_pattern"], output):
        cause = check.get("action_cause", "")
    return alarm, cause


class SSHSession:
    def __init__(self, config):
        self.config = config
        self.client = None
        self.channel = None
        self.limit = 1024 * 1024

    def __enter__(self):
        cfg = self.config
        self.client = paramiko.SSHClient()
        self.client.load_host_keys(cfg["known_hosts"])
        self.client.set_missing_host_key_policy(paramiko.RejectPolicy())
        try:
            self.client.connect(cfg["host"], port=cfg["port"], username=cfg["username"],
                                password=credential(cfg), key_filename=cfg.get("private_key_file"),
                                allow_agent=False, look_for_keys=False, timeout=cfg.get("connect_timeout", 5),
                                banner_timeout=cfg.get("connect_timeout", 5), auth_timeout=cfg.get("connect_timeout", 5),
                                channel_timeout=cfg.get("command_timeout", 5))
            if cfg["transport"] == "shell":
                with self._deadline(cfg.get("command_timeout", 5)):
                    self.channel = self.client.invoke_shell(width=200, height=100)
                    self.channel.settimeout(cfg.get("command_timeout", 5))
                    self._prompt()
            for command in cfg.get("setup_commands", []):
                self.command(command)
            return self
        except BaseException:
            self.client.close()
            raise

    def __exit__(self, *args):
        if self.channel:
            self.channel.close()
        if self.client:
            self.client.close()

    @contextmanager
    def _deadline(self, seconds):
        # Paramiko's shell/exec request acknowledgements can otherwise wait
        # indefinitely even after Channel.settimeout(). Closing the transport
        # releases those waits as well as blocked writes.
        expired = threading.Event()
        client = self.client

        def expire():
            expired.set()
            client.close()

        watchdog = threading.Timer(seconds, expire)
        watchdog.daemon = True
        watchdog.start()
        try:
            yield
        finally:
            watchdog.cancel()
            if expired.is_set():
                raise TimeoutError("SSH operation deadline exceeded")

    def _prompt(self):
        pattern = self.config.get("prompt_pattern", r"(?m)^[<\[][^\r\n]+[>\]]\s*$")
        deadline = time.monotonic() + self.config.get("command_timeout", 5)
        output = ""
        while time.monotonic() < deadline:
            if self.channel.recv_ready():
                chunk = self.channel.recv(65535)
                if not chunk:
                    break
                output += chunk.decode(errors="replace")
                if len(output) > self.limit:
                    raise CheckError("SSH output exceeds 1 MiB")
                if re.search(pattern, output):
                    return output
            elif self.channel.closed:
                break
            else:
                time.sleep(0.01)
        raise TimeoutError("SSH prompt deadline exceeded")

    def command(self, command):
        with self._deadline(self.config.get("command_timeout", 5)):
            return self._command(command)

    def _command(self, command):
        if self.channel:
            self.channel.sendall(command + "\n")
            output = self._prompt()
        else:
            timeout = self.config.get("command_timeout", 5)
            channel = self.client.get_transport().open_session(timeout=timeout)
            try:
                channel.settimeout(timeout)
                channel.exec_command(command)
                deadline, output, error = time.monotonic() + timeout, b"", b""
                while True:
                    if channel.recv_ready():
                        output += channel.recv(65535)
                    if channel.recv_stderr_ready():
                        error += channel.recv_stderr(65535)
                    if len(output) + len(error) > self.limit:
                        raise CheckError("SSH output exceeds 1 MiB")
                    if channel.exit_status_ready() and not channel.recv_ready() and not channel.recv_stderr_ready():
                        if channel.recv_exit_status() != 0 or error:
                            raise CheckError("SSH command failed")
                        break
                    if time.monotonic() >= deadline:
                        raise TimeoutError("SSH command deadline exceeded")
                    time.sleep(0.01)
                output = output.decode(errors="replace")
            finally:
                channel.close()
        if re.search(self.config.get("error_pattern", r"(?i)%\s*(Unrecognized|Incomplete|Ambiguous)|Error:|Permission denied"), output):
            raise CheckError("SSH device rejected a configured command")
        return output


def collect(device, session):
    outputs = {}
    observations = []
    for check in device["checks"]:
        command = check["command"]
        if command not in outputs:
            outputs[command] = session.command(command)
        alarm, cause = evaluate(check, outputs[command])
        observations.append((check, alarm, cause))
    return observations
