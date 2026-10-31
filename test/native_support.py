"""Compile and drive the actual Embedded-BDI runtime without USB hardware."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "test/.runtime/native_agent"


def build_native(sanitize=False):
    sources = sorted((ROOT / "lib/EmbeddedBDI/lib").rglob("*.cpp"))
    dependencies = sources + list((ROOT / "src").glob("*.h")) + [ROOT / "test/native_agent.cpp"]
    if not sanitize and BINARY.exists() and BINARY.stat().st_mtime >= max(p.stat().st_mtime for p in dependencies):
        return BINARY
    BINARY.parent.mkdir(parents=True, exist_ok=True)
    args = ["clang++", "-std=c++11", "-g", "-Wno-ignored-qualifiers", "-I" + str(ROOT / "src"),
            "-I" + str(ROOT / "lib/EmbeddedBDI/lib")]
    if sanitize:
        args += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(args + [str(ROOT / "test/native_agent.cpp")] + [str(p) for p in sources] +
                   ["-o", str(BINARY)], check=True, capture_output=True)
    return BINARY


class NativeAgent:
    def __init__(self, boot="test-boot", capacity=32):
        self.process = subprocess.Popen([str(build_native()), boot, str(capacity)],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        text=True, bufsize=1)
        self.sequence = {}
        self.states = {}
        self.commands = []
        self.lines = []
        self.now = 0

    def send(self, line):
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()
        batch = []
        while True:
            response = self.process.stdout.readline().strip()
            if response == "[SYNC]":
                break
            if not response:
                raise RuntimeError(f"native agent stopped: {self.process.poll()}")
            batch.append(response)
            from reliable_gateway import fields
            f = fields(response)
            if response.startswith("[BDI][STATE]"):
                self.states[f["device"]] = f
            if response.startswith("[BDI][CMD]"):
                self.commands.append(f | {"action": response.split()[1]})
        self.lines.extend(batch)
        return batch

    def event(self, device="a", event="HEARTBEAT", component="system", level="INFO", status="OK", epoch=1, **extra):
        self.sequence[device] = self.sequence.get(device, 0) + 1
        f = dict(device=device, kind=extra.pop("kind", "server"), rack=extra.pop("rack", "rack-1"),
                 epoch=epoch, seq=self.sequence[device], event=event, component=component, level=level, status=status)
        f.update(extra)
        line = "H3C LOG " + " ".join(f"{k}={v}" for k, v in f.items())
        self.send(line)
        return line

    def alarm(self, device="a", event="THERMAL", component="cpu1", **extra):
        return self.event(device, event, component, extra.pop("level", "CRITICAL"),
                          "LOCKED" if event == "AUTH_LOCKED" else "ALARM", **extra)

    def recover(self, device="a", fault="THERMAL", component="cpu1", **extra):
        return self.event(device, "RECOVERY", component, fault=fault, **extra)

    def tick(self, milliseconds):
        self.now += milliseconds
        return self.send("TICK " + str(self.now))

    def ack(self, command=None, status="OK", **overrides):
        f = {k: v for k, v in (command or self.commands[-1]).items()
             if k in {"request_id", "device", "component", "action"}}
        f["status"] = status
        f.update(overrides)
        return self.send("H3C ACK " + " ".join(f"{k}={v}" for k, v in f.items()))

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=10)
        self.process.stdout.close()
        if self.process.returncode:
            raise RuntimeError(f"native agent exited {self.process.returncode}")
