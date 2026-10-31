"""Lab workload; never used by a live SSH configuration."""
from .transport import *

class UsbMqttGateway(GatewayTransport):
    def execute_simulated(self, line):
        if self.args.action_mode == "realistic":
            self.simulation.submit(line)
            return
        f = fields(line)
        action = line.split()[1]
        if action not in {"UNLOCK", "RECOVER_INTERFACE"}:
            return
        incident = f"{f['device']}:{f['component']}:{f['fault']}:{f['incident']}"
        result = self.store.action(incident)
        if result is None:
            result = {"mode": self.args.action_mode, "action": action,
                      "device": f["device"], "component": f["component"], "fault": f["fault"]}
            # Commit before acknowledgement. A retry uses the same ledger entry.
            self.store.save_action(incident, result)
        mode = result["mode"]
        if mode == "timeout":
            return
        self.send_usb(f"H3C ACK action={action} request_id={f['request_id']} device={f['device']} "
                      f"component={f['component']} status={'FAILED' if mode == 'failed' else 'OK'}")
        if mode == "success":
            meta = self.states.get(f["device"], {})
            self.publish_log("INFO", "RECOVERY", "OK", device=f["device"],
                             component=f["component"], fault=f["fault"],
                             kind=meta.get("kind", "server"), rack=meta.get("rack", "rack-1"))
        self.unlock_acks += action == "UNLOCK"


    def random_event(self):
        with self.input_lock:
            if self.args.action_mode == "realistic":
                self.simulation.sample()
            else:
                self._random_event()


    def _random_event(self):
        # Faults persist in a per-device world; normal heartbeat never invents a
        # recovery. Partial repair selects exactly one active component fault.
        sources = self.topology.nodes if self.topology else {
            f"sim-{index + 1:02d}": {} for index in range(self.args.devices)}
        for device, metadata in sources.items():
            source = dict(device=device, kind=metadata.get("kind", "server"), rack=metadata.get("rack", "network"))
            active = self.world.setdefault(device, {})
            if random.random() < self.args.fault_rate:
                if active and random.random() < 0.35:
                    key = random.choice(list(active))
                    component, fault = key.split(":", 1)
                    self.publish_log("INFO", "RECOVERY", "OK", component=component, fault=fault, **source)
                else:
                    event = random.choice(list(EVENTS))
                    component, level = EVENTS[event]
                    key = component + ":" + event
                    if key not in active:
                        self.publish_log(level, event, "LOCKED" if event == "AUTH_LOCKED" else "ALARM",
                                         component=component, **source)
                        active[key] = True
            self.publish_log("INFO", "HEARTBEAT", "OK", **source)


    def smoke_cycle(self):
        self.publish_log("INFO", "HEARTBEAT", "OK")
        self.publish_log("CRITICAL", "THERMAL", "ALARM")
        self.publish_log("INFO", "RECOVERY", "OK", component="cpu1", fault="THERMAL")
        self.publish_log("WARN", "AUTH_LOCKED", "LOCKED")


    def execute_action(self, line):
        self.execute_simulated(line)

    def sample(self):
        self.random_event()

    def initial_sample(self):
        if not self.args.skip_smoke:
            self.smoke_cycle()

    def extra_workers(self):
        if self.args.action_mode == "realistic":
            from .realistic_simulation import RealisticSimulation
            self.simulation = RealisticSimulation(self)
            return (self.simulation.run,)
        return ()


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", default="/dev/cu.usbmodem101")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--mqtt-port", type=int, default=1883)
    p.add_argument("--user", default="esp32")
    p.add_argument("--password", default="esp32-bdi-test")
    p.add_argument("--gateway-id", default="h3c-usb-mqtt-gateway")
    p.add_argument("--state-db", default=str(Path(__file__).resolve().parents[1] / "test/.runtime/gateway.sqlite3"))
    p.add_argument("--min-interval", type=float, default=20)
    p.add_argument("--max-interval", type=float, default=60)
    p.add_argument("--fault-rate", type=float, default=0.05)
    p.add_argument("--devices", type=int, default=3)
    p.add_argument("--duration", type=float, default=0)
    p.add_argument("--seed", type=int)
    p.add_argument("--skip-smoke", action="store_true")
    p.add_argument("--action-mode", choices=["success", "failed", "timeout", "partial", "realistic"], default="success")
    p.add_argument("--workload", choices=["standard", "all_faults", "overnight"], default="standard")
    p.add_argument("--normal-min", type=float, default=300, help="minimum healthy seconds between overnight incidents")
    p.add_argument("--normal-max", type=float, default=900, help="maximum healthy seconds between overnight incidents")
    p.add_argument("--topology", help="optional topology JSON for service quorum and failure-domain correlation")
    args = p.parse_args()
    if not 0 <= args.fault_rate <= 1 or not 1 <= args.devices <= 32:
        p.error("fault-rate must be 0..1; devices must be 1..32")
    if args.min_interval <= 0 or args.max_interval < args.min_interval or args.max_interval >= 80:
        p.error("intervals must satisfy 0 < min <= max < 80 (firmware freshness limit: 90s)")
    if args.duration < 0:
        p.error("duration must be nonnegative")
    if args.workload in {"all_faults", "overnight"} and args.action_mode != "realistic":
        p.error("scenario workloads require --action-mode realistic")
    if not 60 <= args.normal_min <= args.normal_max <= 86400:
        p.error("healthy intervals must satisfy 60 <= normal-min <= normal-max <= 86400")
    return args


def main():
    gateway = UsbMqttGateway(parse_args())
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: gateway.stop_event.set())
    return gateway.run()


if __name__ == "__main__":
    raise SystemExit(main())
