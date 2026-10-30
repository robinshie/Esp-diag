#!/usr/bin/env python3
"""Local-only SSH test double for an H3C S5120V2-LI switch."""

from __future__ import annotations

import argparse
import queue
import signal
import socket
import threading
from collections import deque
from datetime import datetime
from pathlib import Path

import paramiko


PRODUCT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HOST_KEY = PRODUCT_ROOT.parents[3] / "test" / ".runtime" / "s5120v2_ssh_host_key"


class FakeComwareSwitch:
    """Command engine shared by all SSH sessions."""

    def __init__(self, sysname: str = "H3C") -> None:
        self.sysname = sysname
        self.lock = threading.Lock()
        self.logs: deque[str] = deque(maxlen=200)
        self.interface_up = True
        sample_path = PRODUCT_ROOT / "logs" / "samples.log"
        for line in sample_path.read_text(encoding="utf-8").splitlines():
            if line and not line.startswith("#"):
                self.logs.append(line)

    def prompt(self, view: str) -> str:
        if view == "interface":
            return f"[{self.sysname}-GigabitEthernet1/0/1]"
        return f"[{self.sysname}]" if view == "system" else f"<{self.sysname}>"

    def _event_log(self, module: str, severity: int, mnemonic: str, content: str) -> str:
        now = datetime.now().astimezone()
        timestamp = now.strftime("%b %d %H:%M:%S:") + f"{now.microsecond // 1000:03d} " + now.strftime("%Y")
        line = f"%{timestamp} {self.sysname} {module}/{severity}/{mnemonic}: {content}"
        with self.lock:
            self.logs.append(line)
        return line

    def set_interface_state(self, is_up: bool) -> list[str]:
        with self.lock:
            changed = self.interface_up != is_up
            self.interface_up = is_up
        if not changed:
            return []
        state = "up" if is_up else "down"
        return [
            self._event_log(
                "IFNET",
                3,
                "PHY_UPDOWN",
                f"Physical state on the GigabitEthernet1/0/1 changed to {state}.",
            ),
            self._event_log(
                "IFNET",
                5,
                "LINK_UPDOWN",
                f"Line protocol state on the interface GigabitEthernet1/0/1 changed to {state}.",
            ),
        ]

    def _audit(self, command: str, username: str, source_ip: str) -> None:
        now = datetime.now().astimezone()
        timestamp = now.strftime("%b %d %H:%M:%S:") + f"{now.microsecond // 1000:03d} " + now.strftime("%Y")
        line = (
            f"%{timestamp} {self.sysname} SHELL/6/SHELL_CMD: "
            f"-Task=vty0-IPAddr={source_ip}-User={username}; Command is {command}"
        )
        with self.lock:
            self.logs.append(line)

    def execute(
        self,
        command: str,
        view: str = "user",
        username: str = "admin",
        source_ip: str = "127.0.0.1",
    ) -> tuple[str, str, bool, int]:
        command = command.strip()
        if not command:
            return "", view, False, 0

        self._audit(command, username, source_ip)
        lowered = " ".join(command.lower().split())

        if lowered in {"quit", "exit"}:
            if view == "interface":
                return "", "system", False, 0
            if view == "system":
                return "", "user", False, 0
            return "", "user", True, 0
        if lowered == "return":
            return "", "user", False, 0
        if lowered == "system-view":
            return "System View: return to User View with Ctrl+Z.", "system", False, 0
        if lowered == "interface gigabitethernet1/0/1" and view == "system":
            return "", "interface", False, 0
        if lowered == "shutdown" and view == "interface":
            logs = self.set_interface_state(False)
            return "\r\n".join(logs), view, False, 0
        if lowered == "undo shutdown" and view == "interface":
            logs = self.set_interface_state(True)
            return "\r\n".join(logs), view, False, 0
        if lowered == "simulate interface-down gigabitethernet1/0/1":
            logs = self.set_interface_state(False)
            return "SIMULATED FAULT INJECTED\r\n" + "\r\n".join(logs), view, False, 0

        responses = {
            "display version": self._version,
            "display device": self._device,
            "display device manuinfo": self._manuinfo,
            "display environment": self._environment,
            "display cpu-usage": self._cpu,
            "display memory": self._memory,
            "display interface brief": self._interfaces,
            "display stp brief": self._stp,
            "display logbuffer summary": self._log_summary,
            "display logbuffer reverse": self._logbuffer,
            "display current-configuration": self._configuration,
            "display saved-configuration": self._configuration,
            "display info-center": self._info_center,
        }
        handler = responses.get(lowered)
        if handler is not None:
            return handler(), view, False, 0
        if lowered.startswith("ping "):
            target = command.split(maxsplit=1)[1]
            return self._ping(target), view, False, 0

        marker = " " * max(command.find(" "), 0) + "^"
        return (
            f"{marker}\r\n% Unrecognized command found at '^' position.",
            view,
            False,
            1,
        )

    @staticmethod
    def _version() -> str:
        return """H3C Comware Software, Version 7.1.070, Release 6378
Copyright (c) 2004-2026 New H3C Technologies Co., Ltd. All rights reserved.
H3C S5120V2-28P-LI uptime is 0 weeks, 0 days, 1 hour, 23 minutes
SIMULATION NOTICE: This is the ESP-DIAG local SSH test double."""

    @staticmethod
    def _device() -> str:
        return """Slot Type                 State    Subslot  Soft Ver    Patch Ver
1    S5120V2-28P-LI      Master   0        R6378       None"""

    @staticmethod
    def _manuinfo() -> str:
        return """DEVICE_NAME          : S5120V2-28P-LI
DEVICE_SERIAL_NUMBER : SIMULATED-0001
MAC_ADDRESS          : 0200-0000-0001
MANUFACTURING_DATE   : 2026-01-01
VENDOR_NAME          : H3C
SIMULATION           : true"""

    @staticmethod
    def _environment() -> str:
        return """System temperature information (degree centigrade):
Slot  Sensor  Temperature  Lower  Warning  Alarm  Shutdown
1     inflow  31           -10    55       65     75"""

    @staticmethod
    def _cpu() -> str:
        return """Slot 1 CPU 0 CPU usage:
       8% in last 5 seconds
       6% in last 1 minute
       5% in last 5 minutes"""

    @staticmethod
    def _memory() -> str:
        return """Memory statistics are measured in KB:
Slot  Total      Used       Free       FreeRatio
1     524288     173016     351272     67.0%"""

    def _interfaces(self) -> str:
        state = "UP" if self.interface_up else "DOWN"
        return f"""Brief information on interfaces in route mode:
Link: ADM - administratively down; Stby - standby
Interface            Link Protocol Primary IP      Description
Vlan1                UP   UP       192.0.2.2       SIMULATED-MGMT

Brief information on interfaces in bridge mode:
Interface            Link Speed   Duplex Type PVID Description
GE1/0/1              {state:<4} 1G      F      A    10   USER-ACCESS
GE1/0/24             UP   1G      F      T    1    UPLINK"""

    @staticmethod
    def _stp() -> str:
        return """MST ID   Port                                Role  STP State  Protection
0        GigabitEthernet1/0/1                DESI  FORWARDING None
0        GigabitEthernet1/0/24               ROOT  FORWARDING LOOP"""

    def _log_summary(self) -> str:
        with self.lock:
            count = len(self.logs)
        return f"""Log buffer: Enabled
Max buffer size: 1024
Actual buffer size: 512
Dropped messages: 0
Overwritten messages: 0
Current messages: {count}"""

    def _logbuffer(self) -> str:
        with self.lock:
            lines = list(reversed(self.logs))
        return self._log_summary() + "\r\n\r\n" + "\r\n".join(lines)

    def _configuration(self) -> str:
        return f"""#
 version 7.1.070, Release 6378
#
 sysname {self.sysname}
#
 info-center enable
#
 vlan 1
 vlan 10
#
 interface Vlan-interface1
  ip address 192.0.2.2 255.255.255.0
#
 return"""

    @staticmethod
    def _info_center() -> str:
        return """Information Center: Enabled
Console: Enabled, level debugging
Monitor: Disabled, level debugging
Log buffer: Enabled, level informational
Log host: Enabled, level informational"""

    @staticmethod
    def _ping(target: str) -> str:
        return f"""Ping {target} (56 data bytes), press CTRL+C to break
56 bytes from {target}: icmp_seq=0 ttl=64 time=1.1 ms
56 bytes from {target}: icmp_seq=1 ttl=64 time=0.9 ms
--- Ping statistics for {target} ---
2 packet(s) transmitted, 2 packet(s) received, 0.0% packet loss"""


class SSHServerInterface(paramiko.ServerInterface):
    def __init__(self, username: str, password: str) -> None:
        self.username = username
        self.password = password
        self.requests: queue.Queue[tuple[str, object, str]] = queue.Queue()

    def check_auth_password(self, username: str, password: str) -> int:
        if username == self.username and password == self.password:
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    def get_allowed_auths(self, username: str) -> str:
        return "password"

    def check_channel_request(self, kind: str, chanid: int) -> int:
        if kind == "session":
            return paramiko.OPEN_SUCCEEDED
        return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_pty_request(self, channel, term, width, height, pixelwidth, pixelheight, modes) -> bool:
        return True

    def check_channel_shell_request(self, channel) -> bool:
        self.requests.put(("shell", channel, ""))
        return True

    def check_channel_exec_request(self, channel, command: bytes) -> bool:
        self.requests.put(
            ("exec", channel, command.decode("utf-8", errors="replace"))
        )
        return True


class FakeSSHServer:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.switch = FakeComwareSwitch(args.sysname)
        self.stop_event = threading.Event()
        self.listener: socket.socket | None = None
        self.host_key = self._load_host_key(Path(args.host_key))

    @staticmethod
    def _load_host_key(path: Path) -> paramiko.RSAKey:
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.exists():
            return paramiko.RSAKey.from_private_key_file(str(path))
        key = paramiko.RSAKey.generate(2048)
        key.write_private_key_file(str(path))
        path.chmod(0o600)
        return key

    def serve_forever(self) -> None:
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener = listener
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((self.args.host, self.args.port))
        listener.listen(20)
        listener.settimeout(0.5)
        print(
            f"Fake H3C SSH ready: {self.args.host}:{self.args.port} "
            f"user={self.args.user} product=S5120V2-LI",
            flush=True,
        )
        while not self.stop_event.is_set():
            try:
                client, address = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(
                target=self._handle_client,
                args=(client, address),
                daemon=True,
            ).start()
        listener.close()

    def stop(self) -> None:
        self.stop_event.set()
        if self.listener is not None:
            self.listener.close()

    def _handle_client(self, client: socket.socket, address: tuple[str, int]) -> None:
        transport = paramiko.Transport(client)
        transport.local_version = "SSH-2.0-H3C-Comware-7-SIM"
        transport.add_server_key(self.host_key)
        server = SSHServerInterface(self.args.user, self.args.password)
        try:
            transport.start_server(server=server)
            print(f"SSH connected: {address[0]}:{address[1]}", flush=True)
            while transport.is_active():
                channel = transport.accept(0.5)
                if channel is None:
                    continue
                try:
                    request_type, request_channel, command = server.requests.get(
                        timeout=5
                    )
                except queue.Empty:
                    channel.close()
                    continue
                if request_channel is not channel:
                    channel.close()
                    continue
                if request_type == "exec":
                    self._handle_exec(channel, command, address[0])
                else:
                    self._handle_shell(channel, address[0])
        except (EOFError, OSError, paramiko.SSHException) as exc:
            if not self.stop_event.is_set():
                print(f"SSH session error: {exc}", flush=True)
        finally:
            transport.close()
            client.close()

    def _handle_exec(self, channel, command: str, source_ip: str) -> None:
        print(f"SSH RX exec: {command.strip()}", flush=True)
        output, _, _, exit_code = self.switch.execute(
            command,
            username=self.args.user,
            source_ip=source_ip,
        )
        if output:
            channel.sendall((output + "\r\n").encode())
        channel.send_exit_status(exit_code)
        channel.close()

    def _handle_shell(self, channel, source_ip: str) -> None:
        view = "user"
        channel.sendall(
            ("\r\nH3C S5120V2-LI Comware 7 SSH simulator\r\n" + self.switch.prompt(view)).encode()
        )
        buffer = ""
        while not self.stop_event.is_set() and not channel.closed:
            data = channel.recv(1024)
            if not data:
                break
            for char in data.decode("utf-8", errors="replace"):
                if char in "\r\n":
                    if not buffer:
                        continue
                    command = buffer.strip()
                    buffer = ""
                    channel.sendall(b"\r\n")
                    print(f"SSH RX shell: {command}", flush=True)
                    output, view, close_session, _ = self.switch.execute(
                        command,
                        view=view,
                        username=self.args.user,
                        source_ip=source_ip,
                    )
                    if output:
                        channel.sendall((output + "\r\n").encode())
                    if close_session:
                        channel.sendall(b"Bye\r\n")
                        channel.close()
                        return
                    channel.sendall(self.switch.prompt(view).encode())
                elif char in {"\x08", "\x7f"}:
                    buffer = buffer[:-1]
                    channel.sendall(b"\b \b")
                elif char == "\x03":
                    buffer = ""
                    channel.sendall(("^C\r\n" + self.switch.prompt(view)).encode())
                elif char.isprintable():
                    buffer += char
                    channel.sendall(char.encode())


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2222)
    parser.add_argument("--user", default="admin")
    parser.add_argument("--password", default="h3c-test")
    parser.add_argument("--sysname", default="H3C")
    parser.add_argument("--host-key", default=str(DEFAULT_HOST_KEY))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    server = FakeSSHServer(args)

    def stop_handler(signum, frame) -> None:
        server.stop()

    signal.signal(signal.SIGINT, stop_handler)
    signal.signal(signal.SIGTERM, stop_handler)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
