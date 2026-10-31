"""Explicit failure domains and service quorum for scenario simulation.

Correlation is a hint based on configured dependencies, never permission to run
an operation on every member of a failure domain.
"""
from __future__ import annotations

import json
from pathlib import Path


class Topology:
    def __init__(self, config):
        self.config = config
        self.nodes = {n["id"]: n for n in config["nodes"]}
        if len(self.nodes) != len(config["nodes"]):
            raise ValueError("duplicate node ID")
        for service in config["services"]:
            if not 1 <= service["min_healthy"] <= len(service["members"]):
                raise ValueError("invalid quorum")
            for member in service["members"] + service.get("dependencies", []):
                if member not in self.nodes:
                    raise ValueError("unknown service dependency: " + member)
        for node in self.nodes.values():
            for dep in node.get("dependencies", []):
                if dep not in self.nodes:
                    raise ValueError("unknown node dependency: " + dep)
        # Reject cycles, otherwise dependency evaluation could recurse forever.
        def visit(node, path):
            if node in path:
                raise ValueError("cyclic dependency")
            for dep in self.nodes[node].get("dependencies", []):
                visit(dep, path | {node})
        for node in self.nodes:
            visit(node, set())

    @classmethod
    def load(cls, path):
        return cls(json.loads(Path(path).read_text()))

    def evaluate(self, states):
        def healthy(node):
            s = states.get(node, {})
            return (s.get("availability") == "ONLINE" and s.get("state") in {"NORMAL", "DEGRADED"}
                    and all(healthy(dep) for dep in self.nodes[node].get("dependencies", [])))
        services = {}
        for service in self.config["services"]:
            available = sum(healthy(n) for n in service["members"])
            deps_ok = all(healthy(n) for n in service.get("dependencies", []))
            services[service["id"]] = {
                "state": "AVAILABLE" if available >= service["min_healthy"] and deps_ok else "UNAVAILABLE",
                "healthy": available, "required": service["min_healthy"],
            }
        correlations = []
        for dimension in ("rack", "uplink", "storage"):
            groups = {}
            for node, metadata in self.nodes.items():
                if metadata.get(dimension) and not healthy(node):
                    groups.setdefault(metadata[dimension], []).append(node)
            for group, members in groups.items():
                if len(members) >= 2:
                    correlations.append({"domain": dimension, "id": group, "members": members,
                                         "confidence": "SUSPECTED"})
        return {"services": services, "correlations": correlations}


class ClusterWorld:
    """Stateful failure injection; heartbeat production stops for lost nodes."""
    def __init__(self, agent, topology):
        self.agent = agent
        self.topology = topology
        self.stopped = set()

    def pulse(self):
        for node, meta in self.topology.nodes.items():
            if node not in self.stopped:
                self.agent.event(node, kind=meta.get("kind", "server"), rack=meta.get("rack", "network"))

    def rack_power_loss(self, rack):
        for node, meta in self.topology.nodes.items():
            if meta.get("rack") == rack:
                self.agent.alarm(node, "PSU_FAILURE", "psu1", level="WARN", rack=rack)
                self.agent.alarm(node, "PSU_FAILURE", "psu2", rack=rack)
                self.stopped.add(node)

    def uplink_loss(self, switch):
        self.agent.alarm(switch, "LINK_DOWN", "uplink", kind="switch", cause="CABLE_LOSS")
        self.stopped.update(n for n, meta in self.topology.nodes.items() if meta.get("uplink") == switch)

    def node_exit(self, node):
        self.stopped.add(node)

    def storage_loss(self, storage):
        self.agent.alarm(storage, "STORAGE_OFFLINE", "array")
        for node, meta in self.topology.nodes.items():
            if meta.get("storage") == storage:
                self.agent.alarm(node, "IO_TIMEOUT", "data-volume")
