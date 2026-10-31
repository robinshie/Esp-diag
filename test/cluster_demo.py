#!/usr/bin/env python3
"""Run a repeatable cluster failure scenario against the native firmware agent."""
import argparse
import json
from pathlib import Path
from cluster_topology import ClusterWorld, Topology
from native_support import NativeAgent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--scenario", choices=["rack-power", "uplink", "storage", "rolling", "cascade"], default="rack-power")
    p.add_argument("--topology", default=str(Path(__file__).parent / "scenarios/topology.json"))
    p.add_argument("--report", default=str(Path(__file__).parent / ".runtime/cluster-demo.json"))
    args = p.parse_args()
    topology = Topology.load(args.topology)
    agent = NativeAgent()
    world = ClusterWorld(agent, topology)
    steps = []
    def capture(label):
        step = {"step": label, "time_ms": agent.now, "devices": dict(agent.states),
                **topology.evaluate(agent.states), "actions": list(agent.commands)}
        steps.append(step)
        print(json.dumps({"step": label, "services": step["services"], "correlations": step["correlations"]}, ensure_ascii=False))
    try:
        world.pulse(); capture("healthy")
        if args.scenario == "rack-power":
            world.rack_power_loss("rack-1"); capture("two-power-supplies-lost")
            agent.tick(90001); world.pulse(); capture("rack-nodes-offline")
        elif args.scenario == "uplink":
            world.uplink_loss("tor-1"); capture("shared-uplink-failed")
            agent.tick(90001); world.pulse(); capture("partition-detected")
        elif args.scenario == "storage":
            world.storage_loss("san-1"); capture("shared-storage-io-failed")
        elif args.scenario == "rolling":
            for node in ("a", "b", "c"):
                world.node_exit(node); agent.tick(90001); world.pulse(); capture(node + "-offline")
                world.stopped.remove(node); world.pulse(); capture(node + "-rejoined")
        else:
            agent.alarm("a", "FAN_FAILURE", "fan1"); capture("fan-stopped")
            agent.tick(5000); agent.alarm("a"); capture("temperature-critical")
            agent.recover("a"); agent.tick(2001); capture("temperature-recovered-fan-still-failed")
            agent.recover("a", "FAN_FAILURE", "fan1"); agent.tick(2001); capture("fan-replaced")
    finally:
        agent.close()
    output = Path(args.report)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({"scenario": args.scenario, "steps": steps}, indent=2))
    print("Report:", output)


if __name__ == "__main__":
    main()
