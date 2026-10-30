#!/usr/bin/env python3
"""Automated SSH command/response test for the S5120V2-LI simulator."""

from __future__ import annotations

import argparse
import time

import paramiko


def receive_until(channel, marker: str, timeout: float = 5.0) -> str:
    deadline = time.monotonic() + timeout
    received = ""
    while marker not in received and time.monotonic() < deadline:
        if channel.recv_ready():
            received += channel.recv(65535).decode("utf-8", errors="replace")
        else:
            time.sleep(0.05)
    if marker not in received:
        raise TimeoutError(f"timed out waiting for {marker!r}; received={received!r}")
    return received


def run(args: argparse.Namespace) -> None:
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        args.host,
        port=args.port,
        username=args.user,
        password=args.password,
        look_for_keys=False,
        allow_agent=False,
        timeout=5,
    )
    print(f"SSH CONNECTED {args.user}@{args.host}:{args.port}")

    checks = [
        ("display version", "H3C Comware Software"),
        ("display device", "S5120V2-28P-LI"),
        ("display environment", "System temperature information"),
        ("display cpu-usage", "CPU usage"),
        ("display memory", "FreeRatio"),
        ("display interface brief", "GE1/0/24"),
        ("display stp brief", "FORWARDING"),
        ("display logbuffer reverse", "SHELL/6/SHELL_CMD"),
    ]
    for command, expected in checks:
        print(f"CLIENT -> H3C  {command}")
        _, stdout, stderr = client.exec_command(command, timeout=5)
        output = stdout.read().decode("utf-8", errors="replace")
        error = stderr.read().decode("utf-8", errors="replace")
        if error:
            raise RuntimeError(error)
        if expected not in output:
            raise AssertionError(f"{command!r} did not contain {expected!r}: {output}")
        first_line = next((line for line in output.splitlines() if line.strip()), "")
        print(f"H3C -> CLIENT  {first_line}")

    print("CLIENT -> H3C  interactive shell")
    shell = client.invoke_shell(width=120, height=40)
    receive_until(shell, "<H3C>")
    shell.send("system-view\n")
    output = receive_until(shell, "[H3C]")
    if "System View" not in output:
        raise AssertionError(output)
    shell.send("display info-center\n")
    output = receive_until(shell, "[H3C]")
    if "Information Center: Enabled" not in output:
        raise AssertionError(output)
    shell.send("return\n")
    receive_until(shell, "<H3C>")
    shell.send("quit\n")
    receive_until(shell, "Bye")
    shell.close()
    client.close()
    print("PASS: real SSH login, exec commands, interactive views, responses, and logs")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2222)
    parser.add_argument("--user", default="admin")
    parser.add_argument("--password", default="h3c-test")
    return parser.parse_args()


if __name__ == "__main__":
    run(parse_args())
